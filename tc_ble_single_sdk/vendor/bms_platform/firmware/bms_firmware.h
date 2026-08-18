#ifndef BMS_FIRMWARE_H
#define BMS_FIRMWARE_H

#include "bms/bms_application.h"
#include "bms/bmslink.h"

typedef BmsStatus (*BmsFirmwareTransmit)(void *context,
                                         const uint8_t *data,
                                         uint16_t length);
typedef uint8_t (*BmsFirmwareWriteAuthorizer)(void *context);

void bms_firmware_init(BmsFirmwareTransmit transmit, void *context);
void bms_firmware_set_write_authorizer(BmsFirmwareWriteAuthorizer authorizer, void *context);
BmsStatus bms_firmware_receive(const uint8_t *data, uint16_t length);
/* Used by a board adapter or the isolated laboratory simulator. */
BmsStatus bms_firmware_publish_measurement(const BmsMeasurement *measurement);
const BmsRealtime *bms_firmware_realtime(void);

#endif /* BMS_FIRMWARE_H */
