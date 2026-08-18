#include "bms/bms_balance.h"

static uint8_t bms_balance_temperature_allowed(const BmsParameters *parameters,
                                               const BmsRealtime *realtime)
{
    uint8_t index;
    if ((realtime->valid_flags & BMS_MEASUREMENT_VALID_TEMPERATURES) == 0u) {
        return 0u;
    }
    for (index = 0u; index < realtime->temperature_count; ++index) {
        if ((realtime->temperature_decic[index] < parameters->balance_min_temp_decic) ||
            (realtime->temperature_decic[index] > parameters->balance_max_temp_decic)) {
            return 0u;
        }
    }
    return 1u;
}

void bms_balance_init(BmsBalanceState *state)
{
    if (state != 0) {
        state->requested_mask = 0u;
    }
}

uint32_t bms_balance_update(BmsBalanceState *state, const BmsParameters *parameters,
                            const BmsRealtime *realtime, uint32_t protection_flags)
{
    uint8_t index;
    uint32_t mask = 0u;
    int32_t current_ma;
    if ((state == 0) || (parameters == 0) || (realtime == 0) ||
        ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CELLS) == 0u) ||
        (protection_flags != 0u) ||
        (bms_balance_temperature_allowed(parameters, realtime) == 0u)) {
        if (state != 0) {
            state->requested_mask = 0u;
        }
        return 0u;
    }
    current_ma = realtime->current_ma;
    if (current_ma < 0) {
        current_ma = -current_ma;
    }
    if (((realtime->valid_flags & BMS_MEASUREMENT_VALID_CURRENT) == 0u) ||
        ((uint32_t)current_ma > parameters->balance_max_current_ma) ||
        (realtime->cell_max_mv < parameters->balance_start_mv)) {
        state->requested_mask = 0u;
        return 0u;
    }
    for (index = 0u; index < realtime->cell_count; ++index) {
        if ((realtime->cell_voltage_mv[index] >= parameters->balance_start_mv) &&
            ((uint16_t)(realtime->cell_voltage_mv[index] - realtime->cell_min_mv) >=
             parameters->balance_delta_mv)) {
            mask |= (uint32_t)1u << index;
        }
    }
    state->requested_mask = mask;
    return mask;
}
