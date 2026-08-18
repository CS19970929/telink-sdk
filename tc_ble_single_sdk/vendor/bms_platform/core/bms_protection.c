#include "bms/bms_protection.h"

enum {
    BMS_RULE_CELL_OV = 0,
    BMS_RULE_CELL_UV,
    BMS_RULE_CHARGE_OC,
    BMS_RULE_DISCHARGE_OC,
    BMS_RULE_CHARGE_TEMP,
    BMS_RULE_DISCHARGE_TEMP,
    BMS_RULE_AFE_FAULT
};

static uint8_t bms_protection_any_cell_above(const BmsRealtime *realtime, uint16_t threshold)
{
    return ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CELLS) != 0u &&
            realtime->cell_max_mv >= threshold) ? 1u : 0u;
}

static uint8_t bms_protection_any_cell_below(const BmsRealtime *realtime, uint16_t threshold)
{
    return ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CELLS) != 0u &&
            realtime->cell_min_mv <= threshold) ? 1u : 0u;
}

static uint8_t bms_protection_all_cells_below(const BmsRealtime *realtime, uint16_t threshold)
{
    return ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CELLS) != 0u &&
            realtime->cell_max_mv < threshold) ? 1u : 0u;
}

static uint8_t bms_protection_all_cells_above(const BmsRealtime *realtime, uint16_t threshold)
{
    return ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CELLS) != 0u &&
            realtime->cell_min_mv > threshold) ? 1u : 0u;
}

static uint8_t bms_protection_temperature_outside(const BmsRealtime *realtime,
                                                   int16_t minimum,
                                                   int16_t maximum)
{
    uint8_t index;
    if ((realtime->valid_flags & BMS_MEASUREMENT_VALID_TEMPERATURES) == 0u) {
        return 0u;
    }
    for (index = 0u; index < realtime->temperature_count; ++index) {
        if ((realtime->temperature_decic[index] < minimum) ||
            (realtime->temperature_decic[index] > maximum)) {
            return 1u;
        }
    }
    return 0u;
}

static uint8_t bms_protection_update_rule(BmsProtectionMonitor *monitor,
                                          uint8_t rule,
                                          uint32_t flag,
                                          uint8_t trip,
                                          uint8_t release,
                                          uint32_t delay_ms,
                                          uint32_t elapsed_ms)
{
    if (trip != 0u) {
        if (monitor->asserted_ms[rule] < delay_ms) {
            uint32_t remaining = delay_ms - monitor->asserted_ms[rule];
            monitor->asserted_ms[rule] += (elapsed_ms > remaining) ? remaining : elapsed_ms;
        }
        if (monitor->asserted_ms[rule] >= delay_ms) {
            monitor->active_flags |= flag;
        }
    } else {
        monitor->asserted_ms[rule] = 0u;
        if (release != 0u) {
            monitor->active_flags &= ~flag;
        }
    }
    return ((monitor->active_flags & flag) != 0u) ? 1u : 0u;
}

void bms_protection_init(BmsProtectionMonitor *monitor)
{
    uint8_t index;
    if (monitor == 0) {
        return;
    }
    monitor->active_flags = 0u;
    for (index = 0u; index < BMS_PROTECTION_RULE_COUNT; ++index) {
        monitor->asserted_ms[index] = 0u;
    }
}

void bms_protection_evaluate(BmsProtectionMonitor *monitor,
                             const BmsProductConfig *product,
                             const BmsParameters *parameters,
                             const BmsRealtime *realtime,
                             uint32_t elapsed_ms,
                             BmsProtectionResult *result)
{
    uint8_t cell_ov;
    uint8_t cell_uv;
    uint8_t charge_oc;
    uint8_t discharge_oc;
    uint8_t charge_temp;
    uint8_t discharge_temp;
    uint8_t afe_fault;
    uint32_t delay_ms;

    if ((monitor == 0) || (product == 0) || (parameters == 0) ||
        (realtime == 0) || (result == 0)) {
        return;
    }
    delay_ms = parameters->protection_delay_ms;
    cell_ov = bms_protection_any_cell_above(realtime, parameters->cell_ov_trip_mv);
    cell_uv = bms_protection_any_cell_below(realtime, parameters->cell_uv_trip_mv);
    charge_oc = ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CURRENT) != 0u &&
                 realtime->current_ma <= -(int32_t)parameters->charge_oc_trip_ma) ? 1u : 0u;
    discharge_oc = ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CURRENT) != 0u &&
                    realtime->current_ma >= (int32_t)parameters->discharge_oc_trip_ma) ? 1u : 0u;
    charge_temp = bms_protection_temperature_outside(realtime, parameters->charge_temp_low_decic,
                                                      parameters->charge_temp_high_decic);
    discharge_temp = bms_protection_temperature_outside(realtime, parameters->discharge_temp_low_decic,
                                                         parameters->discharge_temp_high_decic);
    afe_fault = (realtime->fault_flags != 0u) ? 1u : 0u;

    result->alarm_flags = 0u;
    result->alarm_flags |= (cell_ov != 0u) ? BMS_ALARM_CELL_OV : 0u;
    result->alarm_flags |= (cell_uv != 0u) ? BMS_ALARM_CELL_UV : 0u;
    result->alarm_flags |= (charge_oc != 0u) ? BMS_ALARM_CHARGE_OC : 0u;
    result->alarm_flags |= (discharge_oc != 0u) ? BMS_ALARM_DISCHARGE_OC : 0u;
    result->alarm_flags |= (charge_temp != 0u) ? BMS_ALARM_CHARGE_TEMP : 0u;
    result->alarm_flags |= (discharge_temp != 0u) ? BMS_ALARM_DISCHARGE_TEMP : 0u;
    result->alarm_flags |= ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CELLS) != 0u &&
                            realtime->cell_delta_mv >= parameters->cell_delta_alarm_mv) ?
                           BMS_ALARM_CELL_DELTA : 0u;
    result->alarm_flags |= (afe_fault != 0u) ? BMS_ALARM_AFE_FAULT : 0u;

    (void)bms_protection_update_rule(monitor, BMS_RULE_CELL_OV, BMS_PROTECTION_CELL_OV,
                                     cell_ov,
                                     bms_protection_all_cells_below(realtime, parameters->cell_ov_release_mv),
                                     delay_ms, elapsed_ms);
    (void)bms_protection_update_rule(monitor, BMS_RULE_CELL_UV, BMS_PROTECTION_CELL_UV,
                                     cell_uv,
                                     bms_protection_all_cells_above(realtime, parameters->cell_uv_release_mv),
                                     delay_ms, elapsed_ms);
    (void)bms_protection_update_rule(monitor, BMS_RULE_CHARGE_OC, BMS_PROTECTION_CHARGE_OC,
                                     charge_oc,
                                     ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CURRENT) != 0u &&
                                      realtime->current_ma > -(int32_t)(parameters->charge_oc_trip_ma / 2u)) ? 1u : 0u,
                                     delay_ms, elapsed_ms);
    (void)bms_protection_update_rule(monitor, BMS_RULE_DISCHARGE_OC, BMS_PROTECTION_DISCHARGE_OC,
                                     discharge_oc,
                                     ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CURRENT) != 0u &&
                                      realtime->current_ma < (int32_t)(parameters->discharge_oc_trip_ma / 2u)) ? 1u : 0u,
                                     delay_ms, elapsed_ms);
    (void)bms_protection_update_rule(monitor, BMS_RULE_CHARGE_TEMP, BMS_PROTECTION_CHARGE_TEMP,
                                     charge_temp,
                                     bms_protection_temperature_outside(realtime,
                                        (int16_t)(parameters->charge_temp_low_decic + 20),
                                        (int16_t)(parameters->charge_temp_high_decic - 20)) == 0u ? 1u : 0u,
                                     delay_ms, elapsed_ms);
    (void)bms_protection_update_rule(monitor, BMS_RULE_DISCHARGE_TEMP, BMS_PROTECTION_DISCHARGE_TEMP,
                                     discharge_temp,
                                     bms_protection_temperature_outside(realtime,
                                        (int16_t)(parameters->discharge_temp_low_decic + 20),
                                        (int16_t)(parameters->discharge_temp_high_decic - 20)) == 0u ? 1u : 0u,
                                     delay_ms, elapsed_ms);
    (void)bms_protection_update_rule(monitor, BMS_RULE_AFE_FAULT, BMS_PROTECTION_AFE_FAULT,
                                     afe_fault, (afe_fault == 0u) ? 1u : 0u, 0u, elapsed_ms);

    result->protection_flags = monitor->active_flags;
    result->power_command.charge_enabled = 1u;
    result->power_command.discharge_enabled = 1u;
    result->power_command.precharge_enabled = 0u;
    if ((result->protection_flags & (BMS_PROTECTION_CELL_OV | BMS_PROTECTION_CHARGE_OC |
                                     BMS_PROTECTION_CHARGE_TEMP | BMS_PROTECTION_AFE_FAULT)) != 0u) {
        result->power_command.charge_enabled = 0u;
    }
    if ((result->protection_flags & (BMS_PROTECTION_CELL_UV | BMS_PROTECTION_DISCHARGE_OC |
                                     BMS_PROTECTION_DISCHARGE_TEMP | BMS_PROTECTION_AFE_FAULT)) != 0u) {
        result->power_command.discharge_enabled = 0u;
    }
    if ((product->power_topology == BMS_POWER_TOPOLOGY_SHARED_PORT) &&
        ((result->power_command.charge_enabled == 0u) ||
         (result->power_command.discharge_enabled == 0u))) {
        result->power_command.charge_enabled = 0u;
        result->power_command.discharge_enabled = 0u;
    }
}
