#include "bms/bms_heating.h"

static int16_t bms_heating_minimum_temperature(const BmsRealtime *realtime)
{
    uint8_t index;
    int16_t minimum = realtime->temperature_decic[0];
    for (index = 1u; index < realtime->temperature_count; ++index) {
        if (realtime->temperature_decic[index] < minimum) {
            minimum = realtime->temperature_decic[index];
        }
    }
    return minimum;
}

void bms_heating_init(BmsHeatingState *state)
{
    if (state != 0) {
        state->requested = 0u;
    }
}

uint8_t bms_heating_update(BmsHeatingState *state, const BmsParameters *parameters,
                           const BmsRealtime *realtime, uint32_t protection_flags)
{
    int16_t minimum;
    if ((state == 0) || (parameters == 0) || (realtime == 0) ||
        ((realtime->valid_flags & BMS_MEASUREMENT_VALID_TEMPERATURES) == 0u) ||
        (realtime->temperature_count == 0u) || (realtime->charger_present == 0u) ||
        (protection_flags != 0u)) {
        if (state != 0) {
            state->requested = 0u;
        }
        return 0u;
    }
    minimum = bms_heating_minimum_temperature(realtime);
    if (minimum <= parameters->heating_start_decic) {
        state->requested = 1u;
    } else if (minimum >= parameters->heating_stop_decic) {
        state->requested = 0u;
    }
    return state->requested;
}
