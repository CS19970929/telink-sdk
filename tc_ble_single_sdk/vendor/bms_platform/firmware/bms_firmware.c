#include "bms_firmware.h"
#include "app_config.h"

#define BMS_FIRMWARE_MAX_PARAMETER_WRITES (21u)

typedef struct {
    BmsLinkDecoder decoder;
    BmsApplication application;
    BmsRealtime realtime;
    BmsFirmwareTransmit transmit;
    void *transmit_context;
    BmsFirmwareWriteAuthorizer write_authorizer;
    void *write_authorizer_context;
} BmsFirmwareContext;

static BmsFirmwareContext g_bms_firmware;

static uint16_t bms_firmware_read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t bms_firmware_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

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
    return g_bms_firmware.transmit(g_bms_firmware.transmit_context, encoded, encoded_length);
}

static void bms_firmware_prepare_response(const BmsLinkFrame *request, BmsLinkFrame *response)
{
    response->version = BMSLINK_VERSION;
    response->flags = BMSLINK_FLAG_RESPONSE;
    response->sequence = request->sequence;
    response->command = request->command;
    response->payload_length = 0u;
}

static BmsStatus bms_firmware_send_error(const BmsLinkFrame *request, BmsStatus failure)
{
    BmsLinkFrame response;
    bms_firmware_prepare_response(request, &response);
    response.flags |= BMSLINK_FLAG_ERROR;
    response.payload_length = 1u;
    response.payload[0] = (uint8_t)bmslink_error_from_status(failure);
    return bms_firmware_send(&response);
}

static uint8_t bms_firmware_write_is_authorized(void)
{
    return ((g_bms_firmware.write_authorizer != 0) &&
            (g_bms_firmware.write_authorizer(g_bms_firmware.write_authorizer_context) != 0u)) ? 1u : 0u;
}

static BmsStatus bms_firmware_write_device_info(const BmsLinkFrame *request)
{
    BmsLinkFrame response;
    const BmsProductConfig *product = &g_bms_firmware.application.product;
    bms_firmware_prepare_response(request, &response);
    response.payload_length = 12u;
    response.payload[0] = 0u;
    response.payload[1] = 2u;
    response.payload[2] = 0u;
    response.payload[3] = 0x51u;
    response.payload[4] = (uint8_t)product->afe_kind;
    response.payload[5] = product->cell_count;
    response.payload[6] = product->temperature_count;
    response.payload[7] = (uint8_t)product->power_topology;
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
    bms_firmware_prepare_response(request, &response);
    bms_firmware_write_u32(&response.payload[offset], realtime->valid_flags); offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->timestamp_ms); offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->pack_voltage_mv); offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], (uint32_t)realtime->current_ma); offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], (uint32_t)realtime->power_mw); offset += 4u;
    bms_firmware_write_u16(&response.payload[offset], realtime->soc_permil); offset += 2u;
    bms_firmware_write_u16(&response.payload[offset], realtime->soh_permil); offset += 2u;
    response.payload[offset++] = realtime->cell_count;
    response.payload[offset++] = realtime->temperature_count;
    for (index = 0u; index < realtime->cell_count; ++index) {
        bms_firmware_write_u16(&response.payload[offset], realtime->cell_voltage_mv[index]); offset += 2u;
    }
    for (index = 0u; index < realtime->temperature_count; ++index) {
        bms_firmware_write_u16(&response.payload[offset], (uint16_t)realtime->temperature_decic[index]); offset += 2u;
    }
    bms_firmware_write_u32(&response.payload[offset], realtime->balance_cells_mask); offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->alarm_flags); offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->protection_flags); offset += 4u;
    bms_firmware_write_u32(&response.payload[offset], realtime->fault_flags); offset += 4u;
    state_flags |= (realtime->power_state.charge_enabled != 0u) ? (1u << 0) : 0u;
    state_flags |= (realtime->power_state.discharge_enabled != 0u) ? (1u << 1) : 0u;
    state_flags |= (realtime->power_state.precharge_enabled != 0u) ? (1u << 2) : 0u;
    state_flags |= (realtime->charger_present != 0u) ? (1u << 3) : 0u;
    state_flags |= (realtime->load_present != 0u) ? (1u << 4) : 0u;
    state_flags |= (realtime->heating_requested != 0u) ? (1u << 5) : 0u;
    response.payload[offset++] = state_flags;
    response.payload_length = offset;
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_write_parameters(const BmsLinkFrame *request)
{
    BmsLinkFrame response;
    const BmsParameterDescriptor *descriptors;
    uint16_t start_id = 0u;
    uint8_t requested_count = 18u;
    uint8_t total;
    uint8_t index;
    uint8_t count = 0u;
    uint16_t offset = 1u;
    if ((request->payload_length != 0u) && (request->payload_length != 3u)) {
        return bms_firmware_send_error(request, BMS_STATUS_INVALID_ARGUMENT);
    }
    if (request->payload_length == 3u) {
        start_id = bms_firmware_read_u16(&request->payload[0]);
        requested_count = request->payload[2];
        if ((requested_count == 0u) || (requested_count > 18u)) {
            requested_count = 18u;
        }
    }
    descriptors = bms_parameters_descriptors(&total);
    bms_firmware_prepare_response(request, &response);
    for (index = 0u; (index < total) && (count < requested_count); ++index) {
        int32_t value;
        if (descriptors[index].id < start_id) {
            continue;
        }
        (void)bms_parameters_get(&g_bms_firmware.application.parameters, descriptors[index].id, &value);
        bms_firmware_write_u16(&response.payload[offset], descriptors[index].id); offset += 2u;
        response.payload[offset++] = descriptors[index].type;
        bms_firmware_write_u32(&response.payload[offset], (uint32_t)value); offset += 4u;
        count++;
    }
    response.payload[0] = count;
    response.payload_length = offset;
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_write_schema(const BmsLinkFrame *request)
{
    BmsLinkFrame response;
    const BmsParameterDescriptor *descriptors;
    uint16_t start_id = 0u;
    uint8_t requested_count = 7u;
    uint8_t total;
    uint8_t index;
    uint8_t count = 0u;
    uint16_t offset = 1u;
    if ((request->payload_length != 0u) && (request->payload_length != 3u)) {
        return bms_firmware_send_error(request, BMS_STATUS_INVALID_ARGUMENT);
    }
    if (request->payload_length == 3u) {
        start_id = bms_firmware_read_u16(&request->payload[0]);
        requested_count = request->payload[2];
        if ((requested_count == 0u) || (requested_count > 7u)) {
            requested_count = 7u;
        }
    }
    descriptors = bms_parameters_descriptors(&total);
    bms_firmware_prepare_response(request, &response);
    for (index = 0u; (index < total) && (count < requested_count); ++index) {
        const BmsParameterDescriptor *descriptor = &descriptors[index];
        if (descriptor->id < start_id) {
            continue;
        }
        bms_firmware_write_u16(&response.payload[offset], descriptor->id); offset += 2u;
        response.payload[offset++] = descriptor->type;
        response.payload[offset++] = descriptor->flags;
        bms_firmware_write_u32(&response.payload[offset], (uint32_t)descriptor->minimum); offset += 4u;
        bms_firmware_write_u32(&response.payload[offset], (uint32_t)descriptor->maximum); offset += 4u;
        bms_firmware_write_u32(&response.payload[offset], (uint32_t)descriptor->default_value); offset += 4u;
        count++;
    }
    response.payload[0] = count;
    response.payload_length = offset;
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_set_parameters(const BmsLinkFrame *request)
{
    BmsParameterWrite writes[BMS_FIRMWARE_MAX_PARAMETER_WRITES];
    BmsLinkFrame response;
    uint8_t count;
    uint8_t index;
    uint16_t offset = 0u;
    BmsStatus status;
    if ((request->payload_length == 0u) || ((request->payload_length % 6u) != 0u) ||
        (request->payload_length > (BMS_FIRMWARE_MAX_PARAMETER_WRITES * 6u))) {
        return bms_firmware_send_error(request, BMS_STATUS_INVALID_ARGUMENT);
    }
    if (bms_firmware_write_is_authorized() == 0u) {
        return bms_firmware_send_error(request, BMS_STATUS_NOT_SUPPORTED);
    }
    count = (uint8_t)(request->payload_length / 6u);
    for (index = 0u; index < count; ++index) {
        writes[index].id = bms_firmware_read_u16(&request->payload[offset]); offset += 2u;
        writes[index].value = (int32_t)bms_firmware_read_u32(&request->payload[offset]); offset += 4u;
    }
    status = bms_application_set_parameters(&g_bms_firmware.application, writes, count,
                                            g_bms_firmware.realtime.timestamp_ms);
    if (status != BMS_STATUS_OK) {
        return bms_firmware_send_error(request, status);
    }
    bms_firmware_prepare_response(request, &response);
    response.payload_length = 1u;
    response.payload[0] = count;
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_write_faults(const BmsLinkFrame *request)
{
    BmsLinkFrame response;
    bms_firmware_prepare_response(request, &response);
    response.payload_length = 12u;
    bms_firmware_write_u32(&response.payload[0], g_bms_firmware.realtime.alarm_flags);
    bms_firmware_write_u32(&response.payload[4], g_bms_firmware.realtime.protection_flags);
    bms_firmware_write_u32(&response.payload[8], g_bms_firmware.realtime.fault_flags);
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_write_event_log(const BmsLinkFrame *request)
{
    BmsLinkFrame response;
    uint8_t start = 0u;
    uint8_t requested = 9u;
    uint8_t count = 0u;
    uint8_t index;
    uint16_t offset = 1u;
    if ((request->payload_length != 0u) && (request->payload_length != 2u)) {
        return bms_firmware_send_error(request, BMS_STATUS_INVALID_ARGUMENT);
    }
    if (request->payload_length == 2u) {
        start = request->payload[0];
        requested = request->payload[1];
        if ((requested == 0u) || (requested > 9u)) {
            requested = 9u;
        }
    }
    bms_firmware_prepare_response(request, &response);
    for (index = start; (index < bms_event_log_count(&g_bms_firmware.application.events)) &&
                          (count < requested); ++index) {
        BmsEvent event;
        (void)bms_event_log_get(&g_bms_firmware.application.events, index, &event);
        bms_firmware_write_u32(&response.payload[offset], event.timestamp_ms); offset += 4u;
        response.payload[offset++] = event.type;
        response.payload[offset++] = event.severity;
        bms_firmware_write_u32(&response.payload[offset], event.before); offset += 4u;
        bms_firmware_write_u32(&response.payload[offset], event.after); offset += 4u;
        count++;
    }
    response.payload[0] = count;
    response.payload_length = offset;
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_control(const BmsLinkFrame *request)
{
    BmsLinkFrame response;
    BmsStatus status;
    if ((request->payload_length != 3u) || (request->payload[0] != 0x01u)) {
        return bms_firmware_send_error(request, BMS_STATUS_NOT_SUPPORTED);
    }
    if (bms_firmware_write_is_authorized() == 0u) {
        return bms_firmware_send_error(request, BMS_STATUS_NOT_SUPPORTED);
    }
    status = bms_application_set_soc(&g_bms_firmware.application,
                                     bms_firmware_read_u16(&request->payload[1]),
                                     g_bms_firmware.realtime.timestamp_ms);
    if (status != BMS_STATUS_OK) {
        return bms_firmware_send_error(request, status);
    }
    g_bms_firmware.realtime.soc_permil = g_bms_firmware.application.soc.soc_permil;
    bms_firmware_prepare_response(request, &response);
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_ota_info(const BmsLinkFrame *request)
{
    BmsLinkFrame response;
    bms_firmware_prepare_response(request, &response);
    response.payload_length = 4u;
    response.payload[0] = (BLE_OTA_SERVER_ENABLE && BMS_OTA_LAYOUT_APPROVED) ? 1u : 0u;
    response.payload[1] = BMS_OTA_LAYOUT_APPROVED ? 1u : 0u;
    response.payload[2] = BMS_OTA_PROCESS_TIMEOUT_SECONDS;
    response.payload[3] = 0u;
    return bms_firmware_send(&response);
}

static BmsStatus bms_firmware_handle_frame(const BmsLinkFrame *request)
{
    if ((request->flags & (BMSLINK_FLAG_RESPONSE | BMSLINK_FLAG_EVENT | BMSLINK_FLAG_ERROR)) != 0u) {
        return bms_firmware_send_error(request, BMS_STATUS_PROTOCOL_ERROR);
    }
    switch ((BmsLinkCommand)request->command) {
    case BMSLINK_COMMAND_GET_DEVICE_INFO: return bms_firmware_write_device_info(request);
    case BMSLINK_COMMAND_GET_REALTIME: return bms_firmware_write_realtime(request);
    case BMSLINK_COMMAND_GET_PARAMETERS: return bms_firmware_write_parameters(request);
    case BMSLINK_COMMAND_SET_PARAMETERS: return bms_firmware_set_parameters(request);
    case BMSLINK_COMMAND_GET_PARAMETER_SCHEMA: return bms_firmware_write_schema(request);
    case BMSLINK_COMMAND_CONTROL: return bms_firmware_control(request);
    case BMSLINK_COMMAND_GET_FAULTS: return bms_firmware_write_faults(request);
    case BMSLINK_COMMAND_GET_EVENT_LOG: return bms_firmware_write_event_log(request);
    case BMSLINK_COMMAND_OTA_INFO: return bms_firmware_ota_info(request);
    default: return bms_firmware_send_error(request, BMS_STATUS_NOT_SUPPORTED);
    }
}

void bms_firmware_init(BmsFirmwareTransmit transmit, void *context)
{
    const BmsProductConfig *product = bms_product_default_config();
    g_bms_firmware.transmit = transmit;
    g_bms_firmware.transmit_context = context;
    g_bms_firmware.write_authorizer = 0;
    g_bms_firmware.write_authorizer_context = 0;
    bmslink_decoder_init(&g_bms_firmware.decoder);
    bms_application_init(&g_bms_firmware.application, product);
    bms_realtime_init(&g_bms_firmware.realtime, product);
    g_bms_firmware.realtime.soc_permil = g_bms_firmware.application.soc.soc_permil;
    g_bms_firmware.realtime.soh_permil = g_bms_firmware.application.soc.soh_permil;
}

void bms_firmware_set_write_authorizer(BmsFirmwareWriteAuthorizer authorizer, void *context)
{
    g_bms_firmware.write_authorizer = authorizer;
    g_bms_firmware.write_authorizer_context = context;
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
        status = bmslink_decoder_push(&g_bms_firmware.decoder, data[index], &request, &frame_ready);
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

BmsStatus bms_firmware_publish_measurement(const BmsMeasurement *measurement)
{
    BmsStatus status;
    uint32_t elapsed_ms;
    if (measurement == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if ((measurement->cell_count != g_bms_firmware.application.product.cell_count) ||
        (measurement->temperature_count != g_bms_firmware.application.product.temperature_count)) {
        return BMS_STATUS_PROTOCOL_ERROR;
    }
    elapsed_ms = (g_bms_firmware.realtime.sample_sequence == 0u) ? 0u :
                 (uint32_t)(measurement->timestamp_ms - g_bms_firmware.realtime.timestamp_ms);
    status = bms_realtime_publish_measurement(&g_bms_firmware.realtime, measurement);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    bms_application_step(&g_bms_firmware.application, &g_bms_firmware.realtime, elapsed_ms);
    g_bms_firmware.realtime.balance_cells_mask =
        g_bms_firmware.application.output.desired_balance_mask;
    g_bms_firmware.realtime.heating_requested =
        g_bms_firmware.application.output.heating_requested;
    g_bms_firmware.realtime.power_state.charge_enabled =
        g_bms_firmware.application.output.desired_power.charge_enabled;
    g_bms_firmware.realtime.power_state.discharge_enabled =
        g_bms_firmware.application.output.desired_power.discharge_enabled;
    g_bms_firmware.realtime.power_state.precharge_enabled =
        g_bms_firmware.application.output.desired_power.precharge_enabled;
    return BMS_STATUS_OK;
}

const BmsRealtime *bms_firmware_realtime(void)
{
    return &g_bms_firmware.realtime;
}
