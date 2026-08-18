#ifndef BMS_HEATING_H
#define BMS_HEATING_H

#include "bms/bms_parameters.h"
#include "bms/bms_realtime.h"

typedef struct {
    uint8_t requested;
} BmsHeatingState;

void bms_heating_init(BmsHeatingState *state);
uint8_t bms_heating_update(BmsHeatingState *state,
                           const BmsParameters *parameters,
                           const BmsRealtime *realtime,
                           uint32_t protection_flags);

#endif /* BMS_HEATING_H */
