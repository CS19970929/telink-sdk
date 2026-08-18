#ifndef BMSLINK_H
#define BMSLINK_H

#include "bms/bms_types.h"

#define BMSLINK_SOF0                 (0xb5u)
#define BMSLINK_SOF1                 (0x4du)
#define BMSLINK_VERSION              (1u)
#define BMSLINK_HEADER_SIZE          (9u)
#define BMSLINK_CRC_SIZE             (2u)
#define BMSLINK_FRAME_OVERHEAD       (BMSLINK_HEADER_SIZE + BMSLINK_CRC_SIZE)
#define BMSLINK_MAX_PAYLOAD          (128u)
#define BMSLINK_MAX_FRAME_SIZE       (BMSLINK_FRAME_OVERHEAD + BMSLINK_MAX_PAYLOAD)

#define BMSLINK_FLAG_RESPONSE        (1u << 0)
#define BMSLINK_FLAG_EVENT           (1u << 1)
#define BMSLINK_FLAG_ERROR           (1u << 2)

typedef enum {
    BMSLINK_COMMAND_GET_DEVICE_INFO = 0x01,
    BMSLINK_COMMAND_GET_REALTIME = 0x02,
    BMSLINK_COMMAND_GET_PARAMETERS = 0x10,
    BMSLINK_COMMAND_SET_PARAMETERS = 0x11,
    BMSLINK_COMMAND_GET_PARAMETER_SCHEMA = 0x12,
    BMSLINK_COMMAND_GET_BLE_NAME = 0x13,
    BMSLINK_COMMAND_SET_BLE_NAME = 0x14,
    BMSLINK_COMMAND_CONTROL = 0x20,
    BMSLINK_COMMAND_GET_FAULTS = 0x30,
    BMSLINK_COMMAND_GET_EVENT_LOG = 0x31,
    BMSLINK_COMMAND_OTA_INFO = 0x40
} BmsLinkCommand;

typedef enum {
    BMSLINK_ERROR_NONE = 0,
    BMSLINK_ERROR_INVALID_COMMAND = 1,
    BMSLINK_ERROR_INVALID_PAYLOAD = 2,
    BMSLINK_ERROR_NOT_READY = 3,
    BMSLINK_ERROR_NOT_SUPPORTED = 4,
    BMSLINK_ERROR_BUSY = 5,
    BMSLINK_ERROR_INTERNAL = 6
} BmsLinkError;

typedef struct {
    uint8_t version;
    uint8_t flags;
    uint16_t sequence;
    uint8_t command;
    uint16_t payload_length;
    uint8_t payload[BMSLINK_MAX_PAYLOAD];
} BmsLinkFrame;

typedef struct {
    uint8_t buffer[BMSLINK_MAX_FRAME_SIZE];
    uint16_t received_length;
    uint16_t expected_length;
} BmsLinkDecoder;

uint16_t bmslink_crc16_ccitt(const uint8_t *data, uint16_t length);
BmsStatus bmslink_encode(const BmsLinkFrame *frame,
                         uint8_t *output,
                         uint16_t output_capacity,
                         uint16_t *output_length);
void bmslink_decoder_init(BmsLinkDecoder *decoder);
BmsStatus bmslink_decoder_push(BmsLinkDecoder *decoder,
                               uint8_t byte,
                               BmsLinkFrame *frame,
                               uint8_t *frame_ready);
BmsLinkError bmslink_error_from_status(BmsStatus status);

#endif /* BMSLINK_H */
