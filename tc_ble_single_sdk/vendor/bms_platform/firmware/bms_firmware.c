#include "bms_firmware.h"

typedef struct {
    BmsLinkDecoder decoder;
    BmsRealtime realtime;
    const BmsProductConfig *product;
    BmsFirmwareTransmit transmit;
    void *transmit_context;
} BmsFirmwareContext;

static BmsFirmwareContext g_bms_firmware;

static void bms_firmware_write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void bms_firmware_write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static BmsStatus bms_firmware_send(const BmsLinkFrame *frame)
{
    uint8_t encoded[BMSLINK_MAX_FRAME_SIZE];
    uint16_t encoded_length;
    BmsStatus status;

    if (g_bms_firmware.transmit == 0) {
        return BMS_STATUS_NOT_READY;
    }
    status = bmslink_encode(frame, encoded, sizeof(encoded), &encoded_length);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    return g_bms_firmware.transmit(g_bms_firmware.transmit_context,
                                   encoded, encoded_length);
}

static BmsStatus bms_firmware_send_error(const BmsLinkFrame *request,
                                         BmsStatus failure)
{
    BmsLinkFrame response;

    response.version = BMSLINK_VERSION;
    response.flags = BMSLINK_FLAG_RESPONSE | BMSLINK_FLAG_ERROR;
    response.sequence = request->sequence;
    response.command = request->command;
    response.payload_length = 1u;
    response.payload[0] = (uint8_t)bmslink_error_from_status(failure);
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_write_device_info(const BmsLinkFrame *request)
{
    BmsLinkFrame response;

    response.version = BMSLINK_VERSION;
    response.flags = BMSLINK_FLAG_RESPONSE;
    response.sequence = request->sequence;
    response.command = request->command;
    response.payload_length = 12u;
    response.payload[0] = 0u;
    response.payload[1] = 1u;
    response.payload[2] = 0u;
    response.payload[3] = 0x51u;
    response.payload[4] = (uint8_t)g_bms_firmware.product->afe_kind;
    response.payload[5] = g_bms_firmware.product->cell_count;
    response.payload[6] = g_bms_firmware.product->temperature_count;
    response.payload[7] = (uint8_t)g_bms_firmware.product->power_topology;
    bms_firmware_write_u32(&response.payload[8], 0u);
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_write_realtime(const BmsLinkFrame *request)
{
    const BmsRealtime *realtime = &g_bms_firmware.realtime;
    BmsLinkFrame response;
    uint16_t offset = 0u;
    uint8_t index;
    uint8_t state_flags = 0u;

    response.version = BMSLINK_VERSION;
    response.flags = BMSLINK_FLAG_RESPONSE;
    response.sequence = request->sequence;
    response.command = request->command;

    bms_firmware_write_u32(&response.payload[offset], realtime->valid_flags);
    offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->timestamp_ms);
    offset += 4u;
    bms_firmware_write_u16(&response.payload[offset], realtime->pack_voltage_mv);
    offset += 2u;
    bms_firmware_write_u32(&response.payload[offset], (uint32_t)realtime->current_ma);
    offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], (uint32_t)realtime->power_mw);
    offset += 4u;
    bms_firmware_write_u16(&response.payload[offset], realtime->soc_permil);
    offset += 2u;
    bms_firmware_write_u16(&response.payload[offset], realtime->soh_permil);
    offset += 2u;
    response.payload[offset++] = realtime->cell_count;
    response.payload[offset++] = realtime->temperature_count;
    for (index = 0u; index < realtime->cell_count; ++index) {
        bms_firmware_write_u16(&response.payload[offset], realtime->cell_voltage_mv[index]);
        offset += 2u;
    }
    for (index = 0u; index < realtime->temperature_count; ++index) {
        bms_firmware_write_u16(&response.payload[offset],
                               (uint16_t)realtime->temperature_decic[index]);
        offset += 2u;
    }
    bms_firmware_write_u16(&response.payload[offset], realtime->cell_min_mv);
    offset += 2u;
    bms_firmware_write_u16(&response.payload[offset], realtime->cell_max_mv);
    offset += 2u;
    bms_firmware_write_u16(&response.payload[offset], realtime->cell_delta_mv);
    offset += 2u;
    bms_firmware_write_u32(&response.payload[offset], realtime->balance_cells_mask);
    offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->alarm_flags);
    offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->protection_flags);
    offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->fault_flags);
    offset += 4u;
    if (realtime->power_state.charge_enabled != 0u) {
        state_flags |= 1u << 0;
    }
    if (realtime->power_state.discharge_enabled != 0u) {
        state_flags |= 1u << 1;
    }
    if (realtime->power_state.precharge_enabled != 0u) {
        state_flags |= 1u << 2;
    }
    if (realtime->charger_present != 0u) {
        state_flags |= 1u << 3;
    }
    if (realtime->load_present != 0u) {
        state_flags |= 1u << 4;
    }
    response.payload[offset++] = state_flags;
    response.payload_length = offset;
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_write_faults(const BmsLinkFrame *request)
{
    BmsLinkFrame response;

    response.version = BMSLINK_VERSION;
    response.flags = BMSLINK_FLAG_RESPONSE;
    response.sequence = request->sequence;
    response.command = request->command;
    response.payload_length = 12u;
    bms_firmware_write_u32(&response.payload[0], g_bms_firmware.realtime.alarm_flags);
    bms_firmware_write_u32(&response.payload[4], g_bms_firmware.realtime.protection_flags);
    bms_firmware_write_u32(&response.payload[8], g_bms_firmware.realtime.fault_flags);
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_handle_frame(const BmsLinkFrame *request)
{
    if ((request->flags & (BMSLINK_FLAG_RESPONSE | BMSLINK_FLAG_EVENT |
                           BMSLINK_FLAG_ERROR)) != 0u) {
        return bms_firmware_send_error(request, BMS_STATUS_PROTOCOL_ERROR);
    }

    switch ((BmsLinkCommand)request->command) {
    case BMSLINK_COMMAND_GET_DEVICE_INFO:
        return bms_firmware_write_device_info(request);
    case BMSLINK_COMMAND_GET_REALTIME:
        return bms_firmware_write_realtime(request);
    case BMSLINK_COMMAND_GET_FAULTS:
        return bms_firmware_write_faults(request);
    default:
        return bms_firmware_send_error(request, BMS_STATUS_NOT_SUPPORTED);
    }
}

void bms_firmware_init(BmsFirmwareTransmit transmit, void *context)
{
    g_bms_firmware.product = bms_product_default_config();
    g_bms_firmware.transmit = transmit;
    g_bms_firmware.transmit_context = context;
    bmslink_decoder_init(&g_bms_firmware.decoder);
    bms_realtime_init(&g_bms_firmware.realtime, g_bms_firmware.product);
}

BmsStatus bms_firmware_receive(const uint8_t *data, uint16_t length)
{
    uint16_t index;
    BmsStatus status;
    BmsLinkFrame request;
    uint8_t frame_ready;

    if ((data == 0) || (length == 0u)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0u; index < length; ++index) {
        status = bmslink_decoder_push(&g_bms_firmware.decoder, data[index],
                                      &request, &frame_ready);
        if ((status != BMS_STATUS_OK) && (status != BMS_STATUS_CRC_ERROR) &&
            (status != BMS_STATUS_PROTOCOL_ERROR)) {
            return status;
        }
        if (frame_ready != 0u) {
            status = bms_firmware_handle_frame(&request);
            if (status != BMS_STATUS_OK) {
                return status;
            }
        }
    }
    return BMS_STATUS_OK;
}

const BmsRealtime *bms_firmware_realtime(void)
{
    return &g_bms_firmware.realtime;
}
