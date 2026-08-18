#ifndef BMS_PLATFORM_H
#define BMS_PLATFORM_H

#include "bms/afe/afe_interface.h"

typedef enum {
    BMS_PLATFORM_STATE_RESET = 0,
    BMS_PLATFORM_STATE_READY,
    BMS_PLATFORM_STATE_AFE_ERROR
} BmsPlatformState;

typedef struct {
    BmsPlatformState state;
    BmsProductConfig product;
    AfeDevice afe;
    BmsRealtime realtime;
    AfeFaultSnapshot afe_faults;
} BmsPlatform;

BmsStatus bms_platform_init(BmsPlatform *platform,
                            const BmsProductConfig *product,
                            const AfeDevice *afe);
BmsStatus bms_platform_poll(BmsPlatform *platform, uint32_t timestamp_ms);

#endif /* BMS_PLATFORM_H */
