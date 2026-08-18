#ifndef BMS_BALANCE_H
#define BMS_BALANCE_H

#include "bms/bms_parameters.h"
#include "bms/bms_realtime.h"

typedef struct {
    uint32_t requested_mask;
} BmsBalanceState;

void bms_balance_init(BmsBalanceState *state);
uint32_t bms_balance_update(BmsBalanceState *state,
                            const BmsParameters *parameters,
                            const BmsRealtime *realtime,
                            uint32_t protection_flags);

#endif /* BMS_BALANCE_H */
