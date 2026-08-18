#include "bms/bms_soc.h"

#define BMS_SOC_REST_CURRENT_MA (100)

static uint16_t bms_soc_clamp(int32_t value)
{
    if (value <= 0) {
        return 0u;
    }
    if (value >= (int32_t)BMS_SOC_PERMIL_MAX) {
        return BMS_SOC_PERMIL_MAX;
    }
    return (uint16_t)value;
}

void bms_soc_init(BmsSocState *state, const BmsParameters *parameters)
{
    if ((state == 0) || (parameters == 0)) {
        return;
    }
    state->soc_permil = parameters->soc_initial_permil;
    state->soh_permil = BMS_SOC_PERMIL_MAX;
    state->charge_residual_ma_ms = 0;
}

BmsStatus bms_soc_set(BmsSocState *state, uint16_t soc_permil)
{
    if ((state == 0) || (soc_permil > BMS_SOC_PERMIL_MAX)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    state->soc_permil = soc_permil;
    state->charge_residual_ma_ms = 0;
    return BMS_STATUS_OK;
}

void bms_soc_update(BmsSocState *state, const BmsParameters *parameters,
                    const BmsRealtime *realtime, uint32_t elapsed_ms)
{
    if ((state == 0) || (parameters == 0) || (realtime == 0) ||
        (parameters->nominal_capacity_mah == 0u) || (elapsed_ms == 0u)) {
        return;
    }
    if (elapsed_ms > 5000u) {
        elapsed_ms = 5000u;
    }
    if ((realtime->valid_flags & BMS_MEASUREMENT_VALID_CURRENT) != 0u) {
        int32_t numerator = state->charge_residual_ma_ms + realtime->current_ma * (int32_t)elapsed_ms;
        int32_t denominator = (int32_t)parameters->nominal_capacity_mah * 3600;
        int32_t delta_permil = numerator / denominator;
        state->charge_residual_ma_ms = numerator % denominator;
        /* Positive current is discharge; negative current is charge. */
        state->soc_permil = bms_soc_clamp((int32_t)state->soc_permil - delta_permil);
    }
    if (((realtime->valid_flags & (BMS_MEASUREMENT_VALID_CELLS |
                                   BMS_MEASUREMENT_VALID_CURRENT)) ==
         (BMS_MEASUREMENT_VALID_CELLS | BMS_MEASUREMENT_VALID_CURRENT)) &&
        (realtime->current_ma >= -BMS_SOC_REST_CURRENT_MA) &&
        (realtime->current_ma <= BMS_SOC_REST_CURRENT_MA)) {
        int32_t ocv_permil = ((int32_t)realtime->cell_min_mv - 3000) * 1000 / 1200;
        int32_t correction;
        ocv_permil = (int32_t)bms_soc_clamp(ocv_permil);
        correction = (ocv_permil - (int32_t)state->soc_permil) / 32;
        state->soc_permil = bms_soc_clamp((int32_t)state->soc_permil + correction);
    }
}
