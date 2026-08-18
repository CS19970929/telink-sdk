#ifndef BMS_SH36735_ADAPTER_H
#define BMS_SH36735_ADAPTER_H

#include "bms/afe/afe_interface.h"
#include "bms/afe/sh36735_driver.h"

typedef struct {
    Sh36735Driver *driver;
    uint8_t cell_count;
    uint8_t temperature_count;
} Sh36735Adapter;

BmsStatus sh36735_adapter_bind(Sh36735Adapter *adapter, AfeDevice *device);

#endif /* BMS_SH36735_ADAPTER_H */
