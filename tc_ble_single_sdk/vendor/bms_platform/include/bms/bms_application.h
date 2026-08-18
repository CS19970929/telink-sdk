#ifndef BMS_APPLICATION_H
#define BMS_APPLICATION_H

#include "bms/bms_balance.h"
#include "bms/bms_event.h"
#include "bms/bms_heating.h"
#include "bms/bms_protection.h"
#include "bms/bms_soc.h"

typedef struct {
    BmsPowerCommand desired_power;
    uint32_t desired_balance_mask;
    uint8_t heating_requested;
} BmsApplicationOutput;

typedef struct {
    BmsProductConfig product;
    BmsParameters parameters;
    BmsProtectionMonitor protection;
    BmsSocState soc;
    BmsBalanceState balance;
    BmsHeatingState heating;
    BmsEventLog events;
    BmsApplicationOutput output;
} BmsApplication;

void bms_application_init(BmsApplication *application, const BmsProductConfig *product);
void bms_application_step(BmsApplication *application,
                          BmsRealtime *realtime,
                          uint32_t elapsed_ms);
BmsStatus bms_application_set_parameters(BmsApplication *application,
                                         const BmsParameterWrite *writes,
                                         uint8_t write_count,
                                         uint32_t timestamp_ms);
BmsStatus bms_application_set_soc(BmsApplication *application,
                                  uint16_t soc_permil,
                                  uint32_t timestamp_ms);

#endif /* BMS_APPLICATION_H */
