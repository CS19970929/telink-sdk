#ifndef BMS_GATT_H
#define BMS_GATT_H

#include "bms/bms_types.h"

void bms_gatt_init(void);
void bms_gatt_process(void);
BmsStatus bms_gatt_transmit(void *context, const uint8_t *data, uint16_t length);
BmsStatus bms_gatt_set_device_name(const uint8_t *name, uint8_t length);
void bms_gatt_ota_started(void);
void bms_gatt_ota_finished(int result);

#endif /* BMS_GATT_H */
