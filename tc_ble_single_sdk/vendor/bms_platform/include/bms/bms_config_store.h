#ifndef BMS_CONFIG_STORE_H
#define BMS_CONFIG_STORE_H

#include "bms/bms_parameters.h"

#define BMS_CONFIG_PARAMETERS_SIZE    (48u)
#define BMS_CONFIG_BLE_NAME_MAX_BYTES (26u)
#define BMS_CONFIG_PAYLOAD_SIZE       (80u)
#define BMS_CONFIG_SLOT_SIZE          (96u)
#define BMS_CONFIG_REQUIRED_SLOTS  (2u)

typedef BmsStatus (*BmsStorageRead)(void *context, uint32_t address,
                                    uint8_t *data, uint16_t length);
typedef BmsStatus (*BmsStorageErase)(void *context, uint32_t address,
                                     uint16_t length);
typedef BmsStatus (*BmsStorageWrite)(void *context, uint32_t address,
                                     const uint8_t *data, uint16_t length);

typedef struct {
    BmsStorageRead read;
    BmsStorageErase erase;
    BmsStorageWrite write;
    void *context;
    uint32_t slot_addresses[BMS_CONFIG_REQUIRED_SLOTS];
    uint16_t slot_size;
} BmsConfigStore;

typedef struct {
    BmsParameters parameters;
    uint8_t ble_name_length;
    uint8_t ble_name[BMS_CONFIG_BLE_NAME_MAX_BYTES];
} BmsPersistentConfig;

/* The board layer supplies a static-lifetime descriptor and two non-overlapping slots. */
BmsStatus bms_config_store_load(const BmsConfigStore *store,
                                BmsPersistentConfig *configuration,
                                uint32_t *generation);
BmsStatus bms_config_store_save(const BmsConfigStore *store,
                                const BmsPersistentConfig *configuration,
                                uint32_t *generation);

#endif /* BMS_CONFIG_STORE_H */
