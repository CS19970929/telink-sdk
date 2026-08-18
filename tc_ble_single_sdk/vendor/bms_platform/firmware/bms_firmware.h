#ifndef BMS_FIRMWARE_H
#define BMS_FIRMWARE_H

#include "bms/bms_application.h"
#include "bms/bms_config_store.h"
#include "bms/bmslink.h"

typedef BmsStatus (*BmsFirmwareTransmit)(void *context,
                                         const uint8_t *data,
                                         uint16_t length);
typedef uint8_t (*BmsFirmwareWriteAuthorizer)(void *context);
typedef BmsStatus (*BmsFirmwareBleNameSetter)(void *context,
                                              const uint8_t *name,
                                              uint8_t length);

void bms_firmware_init(BmsFirmwareTransmit transmit, void *context);
void bms_firmware_set_write_authorizer(BmsFirmwareWriteAuthorizer authorizer, void *context);
void bms_firmware_set_ble_name_setter(BmsFirmwareBleNameSetter setter, void *context);
BmsStatus bms_firmware_set_config_store(const BmsConfigStore *store);
BmsStatus bms_firmware_receive(const uint8_t *data, uint16_t length);
/* Commits deferred Flash writes outside the BLE ATT receive callback. */
void bms_firmware_process(void);
/* Used by a board adapter or the isolated laboratory simulator. */
BmsStatus bms_firmware_publish_measurement(const BmsMeasurement *measurement);
const BmsRealtime *bms_firmware_realtime(void);

#endif /* BMS_FIRMWARE_H */
