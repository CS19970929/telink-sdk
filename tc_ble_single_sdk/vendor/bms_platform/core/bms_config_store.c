#include "bms/bms_config_store.h"

#define BMS_CONFIG_MAGIC        (0x31434642u) /* "BFC1" little endian */
#define BMS_CONFIG_VERSION      (2u)
#define BMS_CONFIG_HEADER_SIZE  (12u)
#define BMS_CONFIG_CRC_OFFSET   (BMS_CONFIG_HEADER_SIZE + BMS_CONFIG_PAYLOAD_SIZE)

/* BLE callback stack is intentionally small on TLSR825x; storage calls are serialized. */
static uint8_t g_bms_config_image[BMS_CONFIG_SLOT_SIZE];
static BmsPersistentConfig g_bms_config_candidate;

static void bms_config_write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void bms_config_write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t bms_config_read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t bms_config_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint32_t bms_config_crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xffffffffu;
    uint8_t bit;
    while (length-- != 0u) {
        crc ^= *data++;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = ((crc & 1u) != 0u) ? ((crc >> 1) ^ 0xedb88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xffffffffu;
}

static void bms_config_encode_parameters(const BmsParameters *parameters, uint8_t *data)
{
    uint16_t offset = 0u;
#define BMS_CONFIG_PUT_U16(value) do { bms_config_write_u16(&data[offset], (uint16_t)(value)); offset += 2u; } while (0)
#define BMS_CONFIG_PUT_U32(value) do { bms_config_write_u32(&data[offset], (uint32_t)(value)); offset += 4u; } while (0)
    BMS_CONFIG_PUT_U16(parameters->cell_ov_trip_mv);
    BMS_CONFIG_PUT_U16(parameters->cell_ov_release_mv);
    BMS_CONFIG_PUT_U16(parameters->cell_uv_trip_mv);
    BMS_CONFIG_PUT_U16(parameters->cell_uv_release_mv);
    BMS_CONFIG_PUT_U16(parameters->protection_delay_ms);
    BMS_CONFIG_PUT_U32(parameters->charge_oc_trip_ma);
    BMS_CONFIG_PUT_U32(parameters->discharge_oc_trip_ma);
    BMS_CONFIG_PUT_U16(parameters->charge_temp_low_decic);
    BMS_CONFIG_PUT_U16(parameters->charge_temp_high_decic);
    BMS_CONFIG_PUT_U16(parameters->discharge_temp_low_decic);
    BMS_CONFIG_PUT_U16(parameters->discharge_temp_high_decic);
    BMS_CONFIG_PUT_U16(parameters->cell_delta_alarm_mv);
    BMS_CONFIG_PUT_U16(parameters->nominal_capacity_mah);
    BMS_CONFIG_PUT_U16(parameters->soc_initial_permil);
    BMS_CONFIG_PUT_U16(parameters->balance_start_mv);
    BMS_CONFIG_PUT_U16(parameters->balance_delta_mv);
    BMS_CONFIG_PUT_U16(parameters->balance_min_temp_decic);
    BMS_CONFIG_PUT_U16(parameters->balance_max_temp_decic);
    BMS_CONFIG_PUT_U32(parameters->balance_max_current_ma);
    BMS_CONFIG_PUT_U16(parameters->heating_start_decic);
    BMS_CONFIG_PUT_U16(parameters->heating_stop_decic);
    while (offset < BMS_CONFIG_PARAMETERS_SIZE) {
        data[offset++] = 0u;
    }
#undef BMS_CONFIG_PUT_U16
#undef BMS_CONFIG_PUT_U32
}

static void bms_config_decode_parameters(const uint8_t *data, BmsParameters *parameters)
{
    uint16_t offset = 0u;
#define BMS_CONFIG_GET_U16(target) do { (target) = bms_config_read_u16(&data[offset]); offset += 2u; } while (0)
#define BMS_CONFIG_GET_U32(target) do { (target) = bms_config_read_u32(&data[offset]); offset += 4u; } while (0)
    BMS_CONFIG_GET_U16(parameters->cell_ov_trip_mv);
    BMS_CONFIG_GET_U16(parameters->cell_ov_release_mv);
    BMS_CONFIG_GET_U16(parameters->cell_uv_trip_mv);
    BMS_CONFIG_GET_U16(parameters->cell_uv_release_mv);
    BMS_CONFIG_GET_U16(parameters->protection_delay_ms);
    BMS_CONFIG_GET_U32(parameters->charge_oc_trip_ma);
    BMS_CONFIG_GET_U32(parameters->discharge_oc_trip_ma);
    BMS_CONFIG_GET_U16(parameters->charge_temp_low_decic);
    BMS_CONFIG_GET_U16(parameters->charge_temp_high_decic);
    BMS_CONFIG_GET_U16(parameters->discharge_temp_low_decic);
    BMS_CONFIG_GET_U16(parameters->discharge_temp_high_decic);
    BMS_CONFIG_GET_U16(parameters->cell_delta_alarm_mv);
    BMS_CONFIG_GET_U16(parameters->nominal_capacity_mah);
    BMS_CONFIG_GET_U16(parameters->soc_initial_permil);
    BMS_CONFIG_GET_U16(parameters->balance_start_mv);
    BMS_CONFIG_GET_U16(parameters->balance_delta_mv);
    BMS_CONFIG_GET_U16(parameters->balance_min_temp_decic);
    BMS_CONFIG_GET_U16(parameters->balance_max_temp_decic);
    BMS_CONFIG_GET_U32(parameters->balance_max_current_ma);
    BMS_CONFIG_GET_U16(parameters->heating_start_decic);
    BMS_CONFIG_GET_U16(parameters->heating_stop_decic);
#undef BMS_CONFIG_GET_U16
#undef BMS_CONFIG_GET_U32
}

static BmsStatus bms_config_validate_ble_name(const uint8_t *name, uint8_t length)
{
    uint8_t index;
    if ((name == 0) || (length == 0u) || (length > BMS_CONFIG_BLE_NAME_MAX_BYTES)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < length; ++index) {
        if ((name[index] < 0x20u) || (name[index] == 0x7fu)) {
            return BMS_STATUS_INVALID_ARGUMENT;
        }
    }
    return BMS_STATUS_OK;
}

static BmsStatus bms_config_encode_payload(const BmsPersistentConfig *configuration, uint8_t *data)
{
    uint16_t index;
    BmsStatus status;
    if (configuration == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = bms_config_validate_ble_name(configuration->ble_name, configuration->ble_name_length);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    bms_config_encode_parameters(&configuration->parameters, data);
    data[BMS_CONFIG_PARAMETERS_SIZE] = configuration->ble_name_length;
    for (index = 0u; index < configuration->ble_name_length; ++index) {
        data[BMS_CONFIG_PARAMETERS_SIZE + 1u + index] = configuration->ble_name[index];
    }
    for (index = (uint16_t)(BMS_CONFIG_PARAMETERS_SIZE + 1u + configuration->ble_name_length);
         index < BMS_CONFIG_PAYLOAD_SIZE; ++index) {
        data[index] = 0u;
    }
    return BMS_STATUS_OK;
}

static BmsStatus bms_config_decode_payload(const uint8_t *data, BmsPersistentConfig *configuration)
{
    uint8_t length;
    uint8_t index;
    BmsStatus status;
    if ((data == 0) || (configuration == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    bms_config_decode_parameters(data, &configuration->parameters);
    length = data[BMS_CONFIG_PARAMETERS_SIZE];
    status = bms_config_validate_ble_name(&data[BMS_CONFIG_PARAMETERS_SIZE + 1u], length);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    configuration->ble_name_length = length;
    for (index = 0u; index < length; ++index) {
        configuration->ble_name[index] = data[BMS_CONFIG_PARAMETERS_SIZE + 1u + index];
    }
    return BMS_STATUS_OK;
}

static BmsStatus bms_config_decode_image(const uint8_t *image, BmsPersistentConfig *configuration,
                                         uint32_t *generation)
{
    BmsPersistentConfig candidate;
    if ((bms_config_read_u32(&image[0]) != BMS_CONFIG_MAGIC) ||
        (bms_config_read_u16(&image[4]) != BMS_CONFIG_VERSION) ||
        (bms_config_read_u16(&image[6]) != BMS_CONFIG_PAYLOAD_SIZE) ||
        (bms_config_read_u32(&image[BMS_CONFIG_CRC_OFFSET]) !=
         bms_config_crc32(image, BMS_CONFIG_CRC_OFFSET))) {
        return BMS_STATUS_CRC_ERROR;
    }
    if ((bms_config_decode_payload(&image[BMS_CONFIG_HEADER_SIZE], &candidate) != BMS_STATUS_OK) ||
        (bms_parameters_validate(&candidate.parameters) != BMS_STATUS_OK)) {
        return BMS_STATUS_PROTOCOL_ERROR;
    }
    *configuration = candidate;
    *generation = bms_config_read_u32(&image[8]);
    return BMS_STATUS_OK;
}

static BmsStatus bms_config_validate_store(const BmsConfigStore *store)
{
    if ((store == 0) || (store->read == 0) || (store->erase == 0) || (store->write == 0) ||
        (store->slot_size < BMS_CONFIG_SLOT_SIZE) ||
        (store->slot_addresses[0] == store->slot_addresses[1])) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    return BMS_STATUS_OK;
}

BmsStatus bms_config_store_load(const BmsConfigStore *store, BmsPersistentConfig *configuration,
                                uint32_t *generation)
{
    uint32_t selected_generation = 0u;
    uint32_t candidate_generation;
    uint8_t selected = 0u;
    uint8_t slot;
    BmsStatus status;
    if ((configuration == 0) || (generation == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = bms_config_validate_store(store);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    *generation = 0u;
    for (slot = 0u; slot < BMS_CONFIG_REQUIRED_SLOTS; ++slot) {
        status = store->read(store->context, store->slot_addresses[slot], g_bms_config_image,
                             sizeof(g_bms_config_image));
        if ((status == BMS_STATUS_OK) &&
            (bms_config_decode_image(g_bms_config_image, &g_bms_config_candidate,
                                     &candidate_generation) == BMS_STATUS_OK)) {
            if ((selected == 0u) || ((int32_t)(candidate_generation - selected_generation) > 0)) {
                *configuration = g_bms_config_candidate;
                selected_generation = candidate_generation;
                selected = 1u;
            }
        }
    }
    *generation = selected_generation;
    return (selected != 0u) ? BMS_STATUS_OK : BMS_STATUS_NOT_READY;
}

BmsStatus bms_config_store_save(const BmsConfigStore *store, const BmsPersistentConfig *configuration,
                                uint32_t *generation)
{
    uint8_t slot;
    uint32_t next_generation;
    uint32_t candidate_generation;
    BmsStatus status;
    if ((configuration == 0) || (generation == 0) ||
        (bms_parameters_validate(&configuration->parameters) != BMS_STATUS_OK)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = bms_config_validate_store(store);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    slot = (uint8_t)((*generation + 1u) & 1u);
    next_generation = *generation + 1u;
    bms_config_write_u32(&g_bms_config_image[0], BMS_CONFIG_MAGIC);
    bms_config_write_u16(&g_bms_config_image[4], BMS_CONFIG_VERSION);
    bms_config_write_u16(&g_bms_config_image[6], BMS_CONFIG_PAYLOAD_SIZE);
    bms_config_write_u32(&g_bms_config_image[8], next_generation);
    status = bms_config_encode_payload(configuration, &g_bms_config_image[BMS_CONFIG_HEADER_SIZE]);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    bms_config_write_u32(&g_bms_config_image[BMS_CONFIG_CRC_OFFSET],
                         bms_config_crc32(g_bms_config_image, BMS_CONFIG_CRC_OFFSET));
    status = store->erase(store->context, store->slot_addresses[slot], store->slot_size);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    status = store->write(store->context, store->slot_addresses[slot], g_bms_config_image,
                          sizeof(g_bms_config_image));
    if (status != BMS_STATUS_OK) {
        return status;
    }
    status = store->read(store->context, store->slot_addresses[slot], g_bms_config_image,
                         sizeof(g_bms_config_image));
    if ((status != BMS_STATUS_OK) ||
        (bms_config_decode_image(g_bms_config_image, &g_bms_config_candidate,
                                 &candidate_generation) != BMS_STATUS_OK) ||
        (candidate_generation != next_generation)) {
        return BMS_STATUS_IO_ERROR;
    }
    *generation = next_generation;
    return BMS_STATUS_OK;
}
