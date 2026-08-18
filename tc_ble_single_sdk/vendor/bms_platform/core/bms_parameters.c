#include "bms/bms_parameters.h"

#define BMS_PARAMETER_COUNT (21u)

static const BmsParameterDescriptor g_descriptors[BMS_PARAMETER_COUNT] = {
    {BMS_PARAM_CELL_OV_TRIP_MV, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 3600, 4500, 4250},
    {BMS_PARAM_CELL_OV_RELEASE_MV, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 3400, 4400, 4150},
    {BMS_PARAM_CELL_UV_TRIP_MV, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 1800, 3300, 2800},
    {BMS_PARAM_CELL_UV_RELEASE_MV, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 2000, 3600, 3000},
    {BMS_PARAM_PROTECTION_DELAY_MS, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 0, 10000, 1000},
    {BMS_PARAM_CHARGE_OC_TRIP_MA, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 1000, 300000, 100000},
    {BMS_PARAM_DISCHARGE_OC_TRIP_MA, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 1000, 300000, 150000},
    {BMS_PARAM_CHARGE_TEMP_LOW_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, -300, 150, 0},
    {BMS_PARAM_CHARGE_TEMP_HIGH_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, 100, 800, 450},
    {BMS_PARAM_DISCHARGE_TEMP_LOW_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, -400, 150, -200},
    {BMS_PARAM_DISCHARGE_TEMP_HIGH_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, 100, 900, 600},
    {BMS_PARAM_CELL_DELTA_ALARM_MV, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 10, 1000, 100},
    {BMS_PARAM_NOMINAL_CAPACITY_MAH, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 100, 60000, 20000},
    {BMS_PARAM_SOC_INITIAL_PERMIL, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 0, 1000, 500},
    {BMS_PARAM_BALANCE_START_MV, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 3000, 4400, 4100},
    {BMS_PARAM_BALANCE_DELTA_MV, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 5, 500, 20},
    {BMS_PARAM_BALANCE_MIN_TEMP_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, -200, 500, 100},
    {BMS_PARAM_BALANCE_MAX_TEMP_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, 0, 900, 450},
    {BMS_PARAM_BALANCE_MAX_CURRENT_MA, BMS_PARAMETER_TYPE_UNSIGNED, 7u, 0, 10000, 500},
    {BMS_PARAM_HEATING_START_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, -300, 300, 50},
    {BMS_PARAM_HEATING_STOP_DECIC, BMS_PARAMETER_TYPE_SIGNED, 7u, -200, 500, 100}
};

const BmsParameterDescriptor *bms_parameters_descriptors(uint8_t *count)
{
    if (count != 0) {
        *count = BMS_PARAMETER_COUNT;
    }
    return g_descriptors;
}

const BmsParameterDescriptor *bms_parameters_find(uint16_t id)
{
    uint8_t index;
    for (index = 0u; index < BMS_PARAMETER_COUNT; ++index) {
        if (g_descriptors[index].id == id) {
            return &g_descriptors[index];
        }
    }
    return 0;
}

void bms_parameters_set_defaults(BmsParameters *parameters)
{
    static const BmsParameters defaults = {
        4250u, 4150u, 2800u, 3000u, 1000u,
        100000u, 150000u,
        0, 450, -200, 600,
        100u, 20000u, 500u,
        4100u, 20u, 100, 450, 500u,
        50, 100
    };
    if (parameters == 0) {
        return;
    }
    *parameters = defaults;
}

BmsStatus bms_parameters_get(const BmsParameters *parameters, uint16_t id, int32_t *value)
{
    if ((parameters == 0) || (value == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    switch ((BmsParameterId)id) {
    case BMS_PARAM_CELL_OV_TRIP_MV: *value = parameters->cell_ov_trip_mv; break;
    case BMS_PARAM_CELL_OV_RELEASE_MV: *value = parameters->cell_ov_release_mv; break;
    case BMS_PARAM_CELL_UV_TRIP_MV: *value = parameters->cell_uv_trip_mv; break;
    case BMS_PARAM_CELL_UV_RELEASE_MV: *value = parameters->cell_uv_release_mv; break;
    case BMS_PARAM_PROTECTION_DELAY_MS: *value = parameters->protection_delay_ms; break;
    case BMS_PARAM_CHARGE_OC_TRIP_MA: *value = (int32_t)parameters->charge_oc_trip_ma; break;
    case BMS_PARAM_DISCHARGE_OC_TRIP_MA: *value = (int32_t)parameters->discharge_oc_trip_ma; break;
    case BMS_PARAM_CHARGE_TEMP_LOW_DECIC: *value = parameters->charge_temp_low_decic; break;
    case BMS_PARAM_CHARGE_TEMP_HIGH_DECIC: *value = parameters->charge_temp_high_decic; break;
    case BMS_PARAM_DISCHARGE_TEMP_LOW_DECIC: *value = parameters->discharge_temp_low_decic; break;
    case BMS_PARAM_DISCHARGE_TEMP_HIGH_DECIC: *value = parameters->discharge_temp_high_decic; break;
    case BMS_PARAM_CELL_DELTA_ALARM_MV: *value = parameters->cell_delta_alarm_mv; break;
    case BMS_PARAM_NOMINAL_CAPACITY_MAH: *value = parameters->nominal_capacity_mah; break;
    case BMS_PARAM_SOC_INITIAL_PERMIL: *value = parameters->soc_initial_permil; break;
    case BMS_PARAM_BALANCE_START_MV: *value = parameters->balance_start_mv; break;
    case BMS_PARAM_BALANCE_DELTA_MV: *value = parameters->balance_delta_mv; break;
    case BMS_PARAM_BALANCE_MIN_TEMP_DECIC: *value = parameters->balance_min_temp_decic; break;
    case BMS_PARAM_BALANCE_MAX_TEMP_DECIC: *value = parameters->balance_max_temp_decic; break;
    case BMS_PARAM_BALANCE_MAX_CURRENT_MA: *value = (int32_t)parameters->balance_max_current_ma; break;
    case BMS_PARAM_HEATING_START_DECIC: *value = parameters->heating_start_decic; break;
    case BMS_PARAM_HEATING_STOP_DECIC: *value = parameters->heating_stop_decic; break;
    default: return BMS_STATUS_NOT_SUPPORTED;
    }
    return BMS_STATUS_OK;
}

static BmsStatus bms_parameters_set_value(BmsParameters *parameters, uint16_t id, int32_t value)
{
    switch ((BmsParameterId)id) {
    case BMS_PARAM_CELL_OV_TRIP_MV: parameters->cell_ov_trip_mv = (uint16_t)value; break;
    case BMS_PARAM_CELL_OV_RELEASE_MV: parameters->cell_ov_release_mv = (uint16_t)value; break;
    case BMS_PARAM_CELL_UV_TRIP_MV: parameters->cell_uv_trip_mv = (uint16_t)value; break;
    case BMS_PARAM_CELL_UV_RELEASE_MV: parameters->cell_uv_release_mv = (uint16_t)value; break;
    case BMS_PARAM_PROTECTION_DELAY_MS: parameters->protection_delay_ms = (uint16_t)value; break;
    case BMS_PARAM_CHARGE_OC_TRIP_MA: parameters->charge_oc_trip_ma = (uint32_t)value; break;
    case BMS_PARAM_DISCHARGE_OC_TRIP_MA: parameters->discharge_oc_trip_ma = (uint32_t)value; break;
    case BMS_PARAM_CHARGE_TEMP_LOW_DECIC: parameters->charge_temp_low_decic = (int16_t)value; break;
    case BMS_PARAM_CHARGE_TEMP_HIGH_DECIC: parameters->charge_temp_high_decic = (int16_t)value; break;
    case BMS_PARAM_DISCHARGE_TEMP_LOW_DECIC: parameters->discharge_temp_low_decic = (int16_t)value; break;
    case BMS_PARAM_DISCHARGE_TEMP_HIGH_DECIC: parameters->discharge_temp_high_decic = (int16_t)value; break;
    case BMS_PARAM_CELL_DELTA_ALARM_MV: parameters->cell_delta_alarm_mv = (uint16_t)value; break;
    case BMS_PARAM_NOMINAL_CAPACITY_MAH: parameters->nominal_capacity_mah = (uint16_t)value; break;
    case BMS_PARAM_SOC_INITIAL_PERMIL: parameters->soc_initial_permil = (uint16_t)value; break;
    case BMS_PARAM_BALANCE_START_MV: parameters->balance_start_mv = (uint16_t)value; break;
    case BMS_PARAM_BALANCE_DELTA_MV: parameters->balance_delta_mv = (uint16_t)value; break;
    case BMS_PARAM_BALANCE_MIN_TEMP_DECIC: parameters->balance_min_temp_decic = (int16_t)value; break;
    case BMS_PARAM_BALANCE_MAX_TEMP_DECIC: parameters->balance_max_temp_decic = (int16_t)value; break;
    case BMS_PARAM_BALANCE_MAX_CURRENT_MA: parameters->balance_max_current_ma = (uint32_t)value; break;
    case BMS_PARAM_HEATING_START_DECIC: parameters->heating_start_decic = (int16_t)value; break;
    case BMS_PARAM_HEATING_STOP_DECIC: parameters->heating_stop_decic = (int16_t)value; break;
    default: return BMS_STATUS_NOT_SUPPORTED;
    }
    return BMS_STATUS_OK;
}

BmsStatus bms_parameters_validate(const BmsParameters *parameters)
{
    if (parameters == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if ((parameters->cell_ov_release_mv >= parameters->cell_ov_trip_mv) ||
        (parameters->cell_uv_release_mv <= parameters->cell_uv_trip_mv) ||
        (parameters->charge_temp_low_decic >= parameters->charge_temp_high_decic) ||
        (parameters->discharge_temp_low_decic >= parameters->discharge_temp_high_decic) ||
        (parameters->balance_min_temp_decic >= parameters->balance_max_temp_decic) ||
        (parameters->heating_start_decic >= parameters->heating_stop_decic)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    return BMS_STATUS_OK;
}

BmsStatus bms_parameters_set(BmsParameters *parameters, uint16_t id, int32_t value)
{
    const BmsParameterDescriptor *descriptor = bms_parameters_find(id);
    BmsParameters candidate;
    BmsStatus status;
    if ((parameters == 0) || (descriptor == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if ((descriptor->flags & BMS_PARAMETER_FLAG_WRITE) == 0u ||
        (value < descriptor->minimum) || (value > descriptor->maximum)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    candidate = *parameters;
    status = bms_parameters_set_value(&candidate, id, value);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    status = bms_parameters_validate(&candidate);
    if (status == BMS_STATUS_OK) {
        *parameters = candidate;
    }
    return status;
}

BmsStatus bms_parameters_set_many(BmsParameters *parameters,
                                   const BmsParameterWrite *writes,
                                   uint8_t write_count)
{
    BmsParameters candidate;
    uint8_t index;
    BmsStatus status;
    if ((parameters == 0) || (writes == 0) || (write_count == 0u)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    candidate = *parameters;
    for (index = 0u; index < write_count; ++index) {
        const BmsParameterDescriptor *descriptor = bms_parameters_find(writes[index].id);
        if ((descriptor == 0) || ((descriptor->flags & BMS_PARAMETER_FLAG_WRITE) == 0u) ||
            (writes[index].value < descriptor->minimum) ||
            (writes[index].value > descriptor->maximum)) {
            return BMS_STATUS_INVALID_ARGUMENT;
        }
        status = bms_parameters_set_value(&candidate, writes[index].id, writes[index].value);
        if (status != BMS_STATUS_OK) {
            return status;
        }
    }
    status = bms_parameters_validate(&candidate);
    if (status == BMS_STATUS_OK) {
        *parameters = candidate;
    }
    return status;
}
