#ifndef BMS_SOC_H
#define BMS_SOC_H

#include "bms/bms_parameters.h"
#include "bms/bms_realtime.h"

typedef struct {
    uint16_t soc_permil;
    uint16_t soh_permil;
    int32_t charge_residual_ma_ms;
} BmsSocState;

void bms_soc_init(BmsSocState *state, const BmsParameters *parameters);
void bms_soc_update(BmsSocState *state,
                    const BmsParameters *parameters,
                    const BmsRealtime *realtime,
                    uint32_t elapsed_ms);
BmsStatus bms_soc_set(BmsSocState *state, uint16_t soc_permil);

#endif /* BMS_SOC_H */
