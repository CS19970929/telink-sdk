#include "bms/bmslink.h"

#define BMSLINK_INDEX_VERSION        (2u)
#define BMSLINK_INDEX_FLAGS          (3u)
#define BMSLINK_INDEX_SEQUENCE_LO    (4u)
#define BMSLINK_INDEX_SEQUENCE_HI    (5u)
#define BMSLINK_INDEX_COMMAND        (6u)
#define BMSLINK_INDEX_LENGTH_LO      (7u)
#define BMSLINK_INDEX_LENGTH_HI      (8u)
#define BMSLINK_INDEX_PAYLOAD        (9u)

static uint16_t bmslink_read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void bmslink_write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void bmslink_decoder_restart(BmsLinkDecoder *decoder, uint8_t byte)
{
    decoder->received_length = 0u;
    decoder->expected_length = 0u;
    if (byte == BMSLINK_SOF0) {
        decoder->buffer[0] = byte;
        decoder->received_length = 1u;
    }
}

uint16_t bmslink_crc16_ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xffffu;
    uint16_t index;
    uint8_t bit;

    if (data == 0) {
        return 0u;
    }

    for (index = 0u; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8;
        for (bit = 0u; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

BmsStatus bmslink_encode(const BmsLinkFrame *frame,
                         uint8_t *output,
                         uint16_t output_capacity,
                         uint16_t *output_length)
{
    uint16_t index;
    uint16_t frame_length;
    uint16_t crc;

    if ((frame == 0) || (output == 0) || (output_length == 0) ||
        (frame->version != BMSLINK_VERSION) ||
        (frame->payload_length > BMSLINK_MAX_PAYLOAD)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    frame_length = (uint16_t)BMSLINK_FRAME_OVERHEAD + frame->payload_length;
    if (output_capacity < frame_length) {
        return BMS_STATUS_NOT_SUPPORTED;
    }

    output[0] = BMSLINK_SOF0;
    output[1] = BMSLINK_SOF1;
    output[BMSLINK_INDEX_VERSION] = frame->version;
    output[BMSLINK_INDEX_FLAGS] = frame->flags;
    bmslink_write_u16_le(&output[BMSLINK_INDEX_SEQUENCE_LO], frame->sequence);
    output[BMSLINK_INDEX_COMMAND] = frame->command;
    bmslink_write_u16_le(&output[BMSLINK_INDEX_LENGTH_LO], frame->payload_length);
    for (index = 0u; index < frame->payload_length; ++index) {
        output[(uint16_t)BMSLINK_INDEX_PAYLOAD + index] = frame->payload[index];
    }

    crc = bmslink_crc16_ccitt(output, (uint16_t)BMSLINK_HEADER_SIZE + frame->payload_length);
    bmslink_write_u16_le(&output[(uint16_t)BMSLINK_HEADER_SIZE + frame->payload_length], crc);
    *output_length = frame_length;
    return BMS_STATUS_OK;
}

void bmslink_decoder_init(BmsLinkDecoder *decoder)
{
    if (decoder != 0) {
        decoder->received_length = 0u;
        decoder->expected_length = 0u;
    }
}

BmsStatus bmslink_decoder_push(BmsLinkDecoder *decoder,
                               uint8_t byte,
                               BmsLinkFrame *frame,
                               uint8_t *frame_ready)
{
    if ((decoder == 0) || (frame == 0) || (frame_ready == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    *frame_ready = 0u;

    if (decoder->received_length == 0u) {
        bmslink_decoder_restart(decoder, byte);
        return BMS_STATUS_OK;
    }
    if ((decoder->received_length == 1u) && (byte != BMSLINK_SOF1)) {
        bmslink_decoder_restart(decoder, byte);
        return BMS_STATUS_OK;
    }
    if (decoder->received_length >= BMSLINK_MAX_FRAME_SIZE) {
        bmslink_decoder_restart(decoder, byte);
        return BMS_STATUS_PROTOCOL_ERROR;
    }

    decoder->buffer[decoder->received_length++] = byte;
    if (decoder->received_length == BMSLINK_HEADER_SIZE) {
        uint16_t payload_length;

        if (decoder->buffer[BMSLINK_INDEX_VERSION] != BMSLINK_VERSION) {
            bmslink_decoder_restart(decoder, byte);
            return BMS_STATUS_PROTOCOL_ERROR;
        }
        payload_length = bmslink_read_u16_le(&decoder->buffer[BMSLINK_INDEX_LENGTH_LO]);
        if (payload_length > BMSLINK_MAX_PAYLOAD) {
            bmslink_decoder_restart(decoder, byte);
            return BMS_STATUS_PROTOCOL_ERROR;
        }
        decoder->expected_length = (uint16_t)BMSLINK_FRAME_OVERHEAD + payload_length;
    }

    if ((decoder->expected_length != 0u) &&
        (decoder->received_length == decoder->expected_length)) {
        uint16_t expected_crc;
        uint16_t actual_crc;
        uint16_t index;

        actual_crc = bmslink_read_u16_le(&decoder->buffer[(uint16_t)BMSLINK_HEADER_SIZE +
                                                             bmslink_read_u16_le(&decoder->buffer[BMSLINK_INDEX_LENGTH_LO])]);
        expected_crc = bmslink_crc16_ccitt(decoder->buffer,
                                           (uint16_t)(decoder->expected_length - BMSLINK_CRC_SIZE));
        if (actual_crc != expected_crc) {
            bmslink_decoder_restart(decoder, byte);
            return BMS_STATUS_CRC_ERROR;
        }

        frame->version = decoder->buffer[BMSLINK_INDEX_VERSION];
        frame->flags = decoder->buffer[BMSLINK_INDEX_FLAGS];
        frame->sequence = bmslink_read_u16_le(&decoder->buffer[BMSLINK_INDEX_SEQUENCE_LO]);
        frame->command = decoder->buffer[BMSLINK_INDEX_COMMAND];
        frame->payload_length = bmslink_read_u16_le(&decoder->buffer[BMSLINK_INDEX_LENGTH_LO]);
        for (index = 0u; index < frame->payload_length; ++index) {
            frame->payload[index] = decoder->buffer[(uint16_t)BMSLINK_INDEX_PAYLOAD + index];
        }
        decoder->received_length = 0u;
        decoder->expected_length = 0u;
        *frame_ready = 1u;
    }

    return BMS_STATUS_OK;
}

BmsLinkError bmslink_error_from_status(BmsStatus status)
{
    switch (status) {
    case BMS_STATUS_OK:
        return BMSLINK_ERROR_NONE;
    case BMS_STATUS_NOT_READY:
        return BMSLINK_ERROR_NOT_READY;
    case BMS_STATUS_NOT_SUPPORTED:
        return BMSLINK_ERROR_NOT_SUPPORTED;
    case BMS_STATUS_INVALID_ARGUMENT:
    case BMS_STATUS_PROTOCOL_ERROR:
    case BMS_STATUS_CRC_ERROR:
        return BMSLINK_ERROR_INVALID_PAYLOAD;
    default:
        return BMSLINK_ERROR_INTERNAL;
    }
}
