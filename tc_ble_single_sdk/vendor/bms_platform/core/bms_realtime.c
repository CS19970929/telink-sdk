#include "bms/bms_realtime.h"

static void bms_realtime_update_cell_extrema(BmsRealtime *realtime)
{
    uint8_t index;
    uint16_t minimum;
    uint16_t maximum;

    if ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CELLS) == 0u) {
        realtime->cell_min_mv = 0u;
        realtime->cell_max_mv = 0u;
        realtime->cell_delta_mv = 0u;
        return;
    }

    minimum = realtime->cell_voltage_mv[0];
    maximum = minimum;
    for (index = 1u; index < realtime->cell_count; ++index) {
        uint16_t voltage = realtime->cell_voltage_mv[index];
        if (voltage < minimum) {
            minimum = voltage;
        }
        if (voltage > maximum) {
            maximum = voltage;
        }
    }

    realtime->cell_min_mv = minimum;
    realtime->cell_max_mv = maximum;
    realtime->cell_delta_mv = (uint16_t)(maximum - minimum);
}

void bms_realtime_init(BmsRealtime *realtime, const BmsProductConfig *config)
{
    uint8_t index;

    if ((realtime == 0) || (config == 0)) {
        return;
    }

    realtime->sample_sequence = 0u;
    realtime->timestamp_ms = 0u;
    realtime->valid_flags = 0u;
    realtime->cell_count = config->cell_count;
    realtime->temperature_count = config->temperature_count;
    realtime->pack_voltage_mv = 0u;
    realtime->current_ma = 0;
    realtime->power_mw = 0;
    realtime->soc_permil = 0u;
    realtime->soh_permil = 0u;
    realtime->cell_min_mv = 0u;
    realtime->cell_max_mv = 0u;
    realtime->cell_delta_mv = 0u;
    realtime->balance_cells_mask = 0u;
    realtime->power_state.charge_enabled = 0u;
    realtime->power_state.discharge_enabled = 0u;
    realtime->power_state.precharge_enabled = 0u;
    realtime->alarm_flags = 0u;
    realtime->protection_flags = 0u;
    realtime->fault_flags = 0u;
    realtime->charger_present = 0u;
    realtime->load_present = 0u;

    for (index = 0u; index < BMS_MAX_CELLS; ++index) {
        realtime->cell_voltage_mv[index] = 0u;
    }
    for (index = 0u; index < BMS_MAX_TEMPERATURES; ++index) {
        realtime->temperature_decic[index] = 0;
    }
}

BmsStatus bms_realtime_publish_measurement(BmsRealtime *realtime,
                                           const BmsMeasurement *measurement)
{
    uint8_t index;

    if ((realtime == 0) || (measurement == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if ((measurement->cell_count == 0u) || (measurement->cell_count > BMS_MAX_CELLS)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (measurement->temperature_count > BMS_MAX_TEMPERATURES) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    realtime->timestamp_ms = measurement->timestamp_ms;
    realtime->valid_flags = measurement->valid_flags;
    realtime->cell_count = measurement->cell_count;
    realtime->temperature_count = measurement->temperature_count;
    realtime->pack_voltage_mv = measurement->pack_voltage_mv;
    realtime->current_ma = measurement->current_ma;
    realtime->charger_present = measurement->charger_present;
    realtime->load_present = measurement->load_present;

    for (index = 0u; index < measurement->cell_count; ++index) {
        realtime->cell_voltage_mv[index] = measurement->cell_voltage_mv[index];
    }
    for (index = measurement->cell_count; index < BMS_MAX_CELLS; ++index) {
        realtime->cell_voltage_mv[index] = 0u;
    }
    for (index = 0u; index < measurement->temperature_count; ++index) {
        realtime->temperature_decic[index] = measurement->temperature_decic[index];
    }
    for (index = measurement->temperature_count; index < BMS_MAX_TEMPERATURES; ++index) {
        realtime->temperature_decic[index] = 0;
    }

    if ((measurement->valid_flags & (BMS_MEASUREMENT_VALID_PACK_VOLTAGE |
                                     BMS_MEASUREMENT_VALID_CURRENT)) ==
        (BMS_MEASUREMENT_VALID_PACK_VOLTAGE | BMS_MEASUREMENT_VALID_CURRENT)) {
        realtime->power_mw = (int32_t)(((int64_t)realtime->pack_voltage_mv *
                                         (int64_t)realtime->current_ma) / 1000);
    } else {
        realtime->power_mw = 0;
    }

    bms_realtime_update_cell_extrema(realtime);
    realtime->sample_sequence++;
    return BMS_STATUS_OK;
}
