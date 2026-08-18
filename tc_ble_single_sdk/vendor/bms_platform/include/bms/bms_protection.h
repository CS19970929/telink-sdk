#ifndef BMS_PROTECTION_H
#define BMS_PROTECTION_H

#include "bms/bms_parameters.h"
#include "bms/bms_realtime.h"

#define BMS_ALARM_CELL_OV             (1u << 0)
#define BMS_ALARM_CELL_UV             (1u << 1)
#define BMS_ALARM_CHARGE_OC           (1u << 2)
#define BMS_ALARM_DISCHARGE_OC        (1u << 3)
#define BMS_ALARM_CHARGE_TEMP         (1u << 4)
#define BMS_ALARM_DISCHARGE_TEMP      (1u << 5)
#define BMS_ALARM_CELL_DELTA          (1u << 6)
#define BMS_ALARM_AFE_FAULT           (1u << 7)

#define BMS_PROTECTION_CELL_OV        (1u << 0)
#define BMS_PROTECTION_CELL_UV        (1u << 1)
#define BMS_PROTECTION_CHARGE_OC      (1u << 2)
#define BMS_PROTECTION_DISCHARGE_OC   (1u << 3)
#define BMS_PROTECTION_CHARGE_TEMP    (1u << 4)
#define BMS_PROTECTION_DISCHARGE_TEMP (1u << 5)
#define BMS_PROTECTION_AFE_FAULT      (1u << 6)

#define BMS_PROTECTION_RULE_COUNT     (7u)

typedef struct {
    uint32_t asserted_ms[BMS_PROTECTION_RULE_COUNT];
    uint32_t active_flags;
} BmsProtectionMonitor;

typedef struct {
    uint32_t alarm_flags;
    uint32_t protection_flags;
    BmsPowerCommand power_command;
} BmsProtectionResult;

void bms_protection_init(BmsProtectionMonitor *monitor);
void bms_protection_evaluate(BmsProtectionMonitor *monitor,
                             const BmsProductConfig *product,
                             const BmsParameters *parameters,
                             const BmsRealtime *realtime,
                             uint32_t elapsed_ms,
                             BmsProtectionResult *result);

#endif /* BMS_PROTECTION_H */
