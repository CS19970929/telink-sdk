#ifndef BMS_SH36735_DRIVER_H
#define BMS_SH36735_DRIVER_H

#include "bms/bms_types.h"

#define SH36735_MAX_CELLS              (20u)
#define SH36735_MAX_TEMPERATURES       (4u)
#define SH36735_SPI_MAX_HZ             (1000000u)

#define SH36735_REG_SCONF1             (0x40u)
#define SH36735_REG_BALANCE_HIGH       (0x55u)
#define SH36735_REG_FLAG1              (0x58u)
#define SH36735_REG_BSTATUS1           (0x5bu)
#define SH36735_REG_TEMP1_HIGH         (0x5du)
#define SH36735_REG_CURRENT_HIGH       (0x67u)
#define SH36735_REG_CELL1_HIGH         (0x69u)
#define SH36735_REG_CADC_HIGH          (0x91u)

typedef BmsStatus (*Sh36735Transfer)(void *context,
                                     const uint8_t *tx,
                                     uint8_t *rx,
                                     uint16_t length);

typedef struct {
    Sh36735Transfer transfer;
    void *transfer_context;
} Sh36735Driver;

uint8_t sh36735_crc8(const uint8_t *data, uint16_t length);
BmsStatus sh36735_read_registers(Sh36735Driver *driver,
                                 uint8_t start_register,
                                 uint8_t *data,
                                 uint8_t length);
BmsStatus sh36735_write_register(Sh36735Driver *driver,
                                 uint8_t register_address,
                                 uint8_t value);
BmsStatus sh36735_software_reset(Sh36735Driver *driver);

#endif /* BMS_SH36735_DRIVER_H */
