#ifndef BMS_SH36735_DRIVER_H
#define BMS_SH36735_DRIVER_H

#include "bms/bms_types.h"

#define SH36735_MAX_CELLS              (20u)
#define SH36735_MAX_TEMPERATURES       (4u)
#define SH36735_SPI_MAX_HZ             (1000000u)

#define SH36735_REG_SCONF1             (0x40u)
#define SH36735_REG_SCONF2             (0x41u)
#define SH36735_REG_SCONF4             (0x43u)
#define SH36735_REG_BALANCE_HIGH       (0x55u)
#define SH36735_REG_BALANCE_MIDDLE     (0x56u)
#define SH36735_REG_BALANCE_LOW        (0x57u)
#define SH36735_REG_FLAG1              (0x58u)
#define SH36735_REG_FLAG2              (0x59u)
#define SH36735_REG_FLAG3              (0x5au)
#define SH36735_REG_BSTATUS1           (0x5bu)
#define SH36735_REG_BSTATUS2           (0x5cu)
#define SH36735_REG_TEMP1_HIGH         (0x5du)
#define SH36735_REG_CURRENT_HIGH       (0x67u)
#define SH36735_REG_CELL1_HIGH         (0x69u)
#define SH36735_REG_CADC_HIGH          (0x91u)
#define SH36735_REG_VTOP_HIGH          (0x93u)
#define SH36735_REG_VCHGR_HIGH         (0x95u)

#define SH36735_SCONF2_LTCLR           (1u << 7)
#define SH36735_SCONF2_PDSG_CONTROL    (1u << 3)
#define SH36735_SCONF2_PDSG_MOS        (1u << 2)
#define SH36735_SCONF2_DSG_MOS         (1u << 1)
#define SH36735_SCONF2_CHG_MOS         (1u << 0)

/* Values written to SCONF1 to request the corresponding IC work mode. */
#define SH36735_SCONF1_NORMAL           (0x00u)
#define SH36735_SCONF1_IDLE             (0x55u)
#define SH36735_SCONF1_SLEEP            (0xaau)

/*
 * The driver deliberately exposes ADC codes separately from engineering
 * values.  Cell divider gain, NTC curve and RSENSE are board properties.
 */
typedef struct {
    uint8_t cell_count;
    uint8_t temperature_count;
    uint16_t cell_code[SH36735_MAX_CELLS];
    uint16_t temperature_code[SH36735_MAX_TEMPERATURES];
    int16_t current_code;
    int16_t cadc_code;
    uint16_t pack_voltage_code;
    uint16_t charger_voltage_code;
    uint8_t flag1;
    uint8_t flag2;
    uint8_t flag3;
    uint8_t bstatus1;
    uint8_t bstatus2;
} Sh36735RawSnapshot;

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
BmsStatus sh36735_set_series_cell_count(Sh36735Driver *driver, uint8_t cell_count);
BmsStatus sh36735_read_raw_snapshot(Sh36735Driver *driver,
                                     uint8_t cell_count,
                                     uint8_t temperature_count,
                                     Sh36735RawSnapshot *snapshot);
BmsStatus sh36735_set_balance_mask(Sh36735Driver *driver, uint32_t cell_mask);
BmsStatus sh36735_set_power_command(Sh36735Driver *driver,
                                     const BmsPowerCommand *command);
BmsStatus sh36735_set_power_mode(Sh36735Driver *driver, uint8_t mode_value);
BmsStatus sh36735_clear_protection_flags(Sh36735Driver *driver,
                                          uint8_t flag1_mask,
                                          uint8_t flag2_mask);

#endif /* BMS_SH36735_DRIVER_H */
