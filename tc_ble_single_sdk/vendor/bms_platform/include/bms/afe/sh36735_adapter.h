#ifndef BMS_SH36735_ADAPTER_H
#define BMS_SH36735_ADAPTER_H

#include "bms/afe/afe_interface.h"
#include "bms/afe/sh36735_driver.h"

/*
 * These conversions belong to the product board: the SH36735 data sheet
 * supplies ADC-code formulae, while RSENSE, the NTC table and calibration
 * belong to a concrete pack. Current must use the BMS convention: positive
 * is discharge and negative is charge.
 */
typedef BmsStatus (*Sh36735CellVoltageConverter)(void *context,
                                                  uint8_t cell_index,
                                                  uint16_t code,
                                                  uint16_t *millivolts);
typedef BmsStatus (*Sh36735TemperatureConverter)(void *context,
                                                  uint8_t temperature_index,
                                                  uint16_t code,
                                                  int16_t *decicelsius);
typedef BmsStatus (*Sh36735CurrentConverter)(void *context,
                                              int16_t code,
                                              int32_t *milliamps);
typedef BmsStatus (*Sh36735ChargerDetector)(void *context,
                                             uint16_t charger_voltage_code,
                                             uint8_t bstatus2,
                                             uint8_t *present);
typedef BmsStatus (*Sh36735LoadDetector)(void *context,
                                          uint8_t bstatus2,
                                          uint8_t *present);

typedef struct {
    Sh36735Driver *driver;
    uint8_t cell_count;
    uint8_t temperature_count;
    uint8_t configure_cell_count_on_init;
    uint8_t allow_balance_control;
    uint8_t allow_power_control;
    void *conversion_context;
    Sh36735CellVoltageConverter cell_voltage_from_code;
    Sh36735TemperatureConverter temperature_from_code;
    Sh36735CurrentConverter current_from_code;
    Sh36735ChargerDetector charger_from_code;
    Sh36735LoadDetector load_from_status;
} Sh36735Adapter;

BmsStatus sh36735_adapter_bind(Sh36735Adapter *adapter, AfeDevice *device);

#endif /* BMS_SH36735_ADAPTER_H */
