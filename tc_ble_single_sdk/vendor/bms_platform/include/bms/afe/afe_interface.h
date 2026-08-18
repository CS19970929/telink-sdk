#ifndef BMS_AFE_INTERFACE_H
#define BMS_AFE_INTERFACE_H

#include "bms/bms_realtime.h"

#define AFE_CAPABILITY_HARDWARE_BALANCE   (1u << 0)
#define AFE_CAPABILITY_CHARGE_SWITCH      (1u << 1)
#define AFE_CAPABILITY_DISCHARGE_SWITCH   (1u << 2)
#define AFE_CAPABILITY_PRECHARGE_SWITCH   (1u << 3)
#define AFE_CAPABILITY_HARDWARE_PROTECTION (1u << 4)
#define AFE_CAPABILITY_COULOMB_COUNTER    (1u << 5)
#define AFE_CAPABILITY_CHARGER_DETECT     (1u << 6)
#define AFE_CAPABILITY_LOAD_DETECT        (1u << 7)
#define AFE_CAPABILITY_SLEEP              (1u << 8)

#define AFE_HARDWARE_FAULT_CELL_OV        (1u << 0)
#define AFE_HARDWARE_FAULT_CELL_UV        (1u << 1)
#define AFE_HARDWARE_FAULT_CHARGE_OC      (1u << 2)
#define AFE_HARDWARE_FAULT_DISCHARGE_OC   (1u << 3)
#define AFE_HARDWARE_FAULT_SHORT_CIRCUIT  (1u << 4)
#define AFE_HARDWARE_FAULT_CHARGE_TEMP    (1u << 5)
#define AFE_HARDWARE_FAULT_DISCHARGE_TEMP (1u << 6)
#define AFE_HARDWARE_FAULT_INTERNAL_TEMP  (1u << 7)
#define AFE_HARDWARE_FAULT_OPEN_WIRE      (1u << 8)
#define AFE_HARDWARE_FAULT_WATCHDOG       (1u << 9)
#define AFE_HARDWARE_FAULT_COMMUNICATION  (1u << 10)

typedef enum {
    AFE_POWER_MODE_NORMAL = 0,
    AFE_POWER_MODE_IDLE,
    AFE_POWER_MODE_SLEEP
} AfePowerMode;

typedef struct {
    uint8_t max_cell_count;
    uint8_t max_temperature_count;
    uint32_t feature_flags;
} AfeCapabilities;

typedef struct {
    uint32_t common_flags;
    uint32_t vendor_status;
} AfeFaultSnapshot;

typedef struct AfeOps {
    BmsStatus (*init)(void *context);
    BmsStatus (*read_measurement)(void *context, BmsMeasurement *measurement);
    BmsStatus (*set_balance)(void *context, uint32_t cell_mask);
    BmsStatus (*set_power)(void *context, const BmsPowerCommand *command);
    BmsStatus (*get_faults)(void *context, AfeFaultSnapshot *faults);
    BmsStatus (*set_power_mode)(void *context, AfePowerMode mode);
} AfeOps;

typedef struct {
    const AfeOps *ops;
    const AfeCapabilities *capabilities;
    void *context;
} AfeDevice;

BmsStatus afe_init(AfeDevice *device);
BmsStatus afe_read_measurement(AfeDevice *device, BmsMeasurement *measurement);
BmsStatus afe_set_balance(AfeDevice *device, uint32_t cell_mask);
BmsStatus afe_set_power(AfeDevice *device, const BmsPowerCommand *command);
BmsStatus afe_get_faults(AfeDevice *device, AfeFaultSnapshot *faults);
BmsStatus afe_set_power_mode(AfeDevice *device, AfePowerMode mode);

#endif /* BMS_AFE_INTERFACE_H */
