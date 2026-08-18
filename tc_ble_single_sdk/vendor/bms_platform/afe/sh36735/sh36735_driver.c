#include "bms/afe/sh36735_driver.h"

#define SH36735_COMMAND_WRITE          (0x01u)
#define SH36735_COMMAND_READ           (0x02u)
#define SH36735_COMMAND_SOFTWARE_RESET (0x0bu)
#define SH36735_SPI_IDLE_BYTE          (0x00u)
#define SH36735_SPI_READ_INITIAL_BYTE  (0xffu)
#define SH36735_SPI_ACK                (0xa5u)
#define SH36735_RAW_FLAG_LENGTH         (5u)
#define SH36735_RAW_MEASUREMENT_LENGTH  (58u)

static uint16_t sh36735_read_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

uint8_t sh36735_crc8(const uint8_t *data, uint16_t length)
{
    uint16_t index;
    uint8_t bit;
    uint8_t crc = 0u;

    if (data == 0) {
        return 0u;
    }

    for (index = 0u; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

BmsStatus sh36735_write_register(Sh36735Driver *driver,
                                 uint8_t register_address,
                                 uint8_t value)
{
    uint8_t tx[5];
    uint8_t rx[5];
    BmsStatus status;

    if ((driver == 0) || (driver->transfer == 0) ||
        (register_address < 0x40u) || (register_address > 0x59u)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    tx[0] = SH36735_COMMAND_WRITE;
    tx[1] = register_address;
    tx[2] = value;
    tx[3] = sh36735_crc8(tx, 3u);
    tx[4] = SH36735_SPI_IDLE_BYTE;

    status = driver->transfer(driver->transfer_context, tx, rx, 5u);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    return (rx[4] == SH36735_SPI_ACK) ? BMS_STATUS_OK : BMS_STATUS_PROTOCOL_ERROR;
}

BmsStatus sh36735_read_registers(Sh36735Driver *driver,
                                 uint8_t start_register,
                                 uint8_t *data,
                                 uint8_t length)
{
    uint8_t tx[96];
    uint8_t rx[96];
    uint8_t index;
    uint8_t crc_input[96];
    uint16_t frame_length;
    BmsStatus status;

    if ((driver == 0) || (driver->transfer == 0) || (data == 0) ||
        (length == 0u) || (start_register < 0x40u) ||
        ((uint16_t)start_register + (uint16_t)length - 1u > 0x99u)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    frame_length = (uint16_t)length + 5u;
    tx[0] = SH36735_COMMAND_READ;
    tx[1] = start_register;
    tx[2] = length;
    for (index = 3u; index < frame_length; ++index) {
        tx[index] = SH36735_SPI_IDLE_BYTE;
    }

    status = driver->transfer(driver->transfer_context, tx, rx, frame_length);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    if ((rx[0] != SH36735_SPI_READ_INITIAL_BYTE) ||
        (rx[1] != SH36735_COMMAND_READ) ||
        (rx[2] != start_register) ||
        (rx[3] != length)) {
        return BMS_STATUS_PROTOCOL_ERROR;
    }

    crc_input[0] = rx[0];
    for (index = 0u; index < (uint8_t)(length + 3u); ++index) {
        crc_input[(uint8_t)(index + 1u)] = rx[(uint8_t)(index + 1u)];
    }
    if (sh36735_crc8(crc_input, (uint16_t)length + 4u) !=
        rx[(uint16_t)length + 4u]) {
        return BMS_STATUS_CRC_ERROR;
    }

    for (index = 0u; index < length; ++index) {
        data[index] = rx[(uint8_t)(index + 4u)];
    }
    return BMS_STATUS_OK;
}

BmsStatus sh36735_software_reset(Sh36735Driver *driver)
{
    uint8_t tx[5];
    uint8_t rx[5];
    BmsStatus status;

    if ((driver == 0) || (driver->transfer == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    tx[0] = SH36735_COMMAND_SOFTWARE_RESET;
    tx[1] = 0xbbu;
    tx[2] = 0xccu;
    tx[3] = sh36735_crc8(tx, 3u);
    tx[4] = SH36735_SPI_IDLE_BYTE;

    status = driver->transfer(driver->transfer_context, tx, rx, 5u);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    return (rx[4] == SH36735_SPI_ACK) ? BMS_STATUS_OK : BMS_STATUS_PROTOCOL_ERROR;
}

BmsStatus sh36735_set_series_cell_count(Sh36735Driver *driver, uint8_t cell_count)
{
    uint8_t configuration;
    BmsStatus status;

    if ((cell_count < 4u) || (cell_count > SH36735_MAX_CELLS)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = sh36735_read_registers(driver, SH36735_REG_SCONF4, &configuration, 1u);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    configuration = (uint8_t)((configuration & 0xe0u) | cell_count);
    return sh36735_write_register(driver, SH36735_REG_SCONF4, configuration);
}

BmsStatus sh36735_read_raw_snapshot(Sh36735Driver *driver,
                                     uint8_t cell_count,
                                     uint8_t temperature_count,
                                     Sh36735RawSnapshot *snapshot)
{
    uint8_t flags[SH36735_RAW_FLAG_LENGTH];
    uint8_t raw[SH36735_RAW_MEASUREMENT_LENGTH];
    uint8_t index;
    BmsStatus status;

    if ((snapshot == 0) || (cell_count < 4u) || (cell_count > SH36735_MAX_CELLS) ||
        (temperature_count > SH36735_MAX_TEMPERATURES)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    status = sh36735_read_registers(driver, SH36735_REG_FLAG1, flags, sizeof(flags));
    if (status != BMS_STATUS_OK) {
        return status;
    }
    status = sh36735_read_registers(driver, SH36735_REG_TEMP1_HIGH, raw, sizeof(raw));
    if (status != BMS_STATUS_OK) {
        return status;
    }

    snapshot->cell_count = cell_count;
    snapshot->temperature_count = temperature_count;
    snapshot->flag1 = flags[0];
    snapshot->flag2 = flags[1];
    snapshot->flag3 = flags[2];
    snapshot->bstatus1 = flags[3];
    snapshot->bstatus2 = flags[4];
    for (index = 0u; index < temperature_count; ++index) {
        snapshot->temperature_code[index] = sh36735_read_u16_be(&raw[(uint8_t)(index * 2u)]);
    }
    for (index = temperature_count; index < SH36735_MAX_TEMPERATURES; ++index) {
        snapshot->temperature_code[index] = 0u;
    }
    snapshot->current_code = (int16_t)sh36735_read_u16_be(&raw[10u]);
    for (index = 0u; index < cell_count; ++index) {
        snapshot->cell_code[index] =
            sh36735_read_u16_be(&raw[(uint8_t)(12u + index * 2u)]);
    }
    for (index = cell_count; index < SH36735_MAX_CELLS; ++index) {
        snapshot->cell_code[index] = 0u;
    }
    snapshot->cadc_code = (int16_t)sh36735_read_u16_be(&raw[52u]);
    snapshot->pack_voltage_code = sh36735_read_u16_be(&raw[54u]);
    snapshot->charger_voltage_code = sh36735_read_u16_be(&raw[56u]);
    return BMS_STATUS_OK;
}

BmsStatus sh36735_set_balance_mask(Sh36735Driver *driver, uint32_t cell_mask)
{
    BmsStatus status;

    if ((cell_mask & 0xfff00000u) != 0u) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = sh36735_write_register(driver, SH36735_REG_BALANCE_HIGH,
                                     (uint8_t)((cell_mask >> 16) & 0x0fu));
    if (status != BMS_STATUS_OK) {
        return status;
    }
    status = sh36735_write_register(driver, SH36735_REG_BALANCE_MIDDLE,
                                     (uint8_t)(cell_mask >> 8));
    if (status != BMS_STATUS_OK) {
        return status;
    }
    return sh36735_write_register(driver, SH36735_REG_BALANCE_LOW, (uint8_t)cell_mask);
}

BmsStatus sh36735_set_power_command(Sh36735Driver *driver,
                                     const BmsPowerCommand *command)
{
    uint8_t configuration;
    BmsStatus status;

    if (command == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = sh36735_read_registers(driver, SH36735_REG_SCONF2, &configuration, 1u);
    if (status != BMS_STATUS_OK) {
        return status;
    }

    if (command->charge_enabled != 0u) {
        configuration |= SH36735_SCONF2_CHG_MOS;
    } else {
        configuration &= (uint8_t)~SH36735_SCONF2_CHG_MOS;
    }
    if (command->discharge_enabled != 0u) {
        configuration |= SH36735_SCONF2_DSG_MOS;
    } else {
        configuration &= (uint8_t)~SH36735_SCONF2_DSG_MOS;
    }
    configuration &= (uint8_t)~SH36735_SCONF2_PDSG_MOS;
    if (command->precharge_enabled != 0u) {
        configuration |= SH36735_SCONF2_PDSG_CONTROL;
    } else {
        configuration &= (uint8_t)~SH36735_SCONF2_PDSG_CONTROL;
    }
    return sh36735_write_register(driver, SH36735_REG_SCONF2, configuration);
}

BmsStatus sh36735_set_power_mode(Sh36735Driver *driver, uint8_t mode_value)
{
    if ((mode_value != SH36735_SCONF1_NORMAL) &&
        (mode_value != SH36735_SCONF1_IDLE) &&
        (mode_value != SH36735_SCONF1_SLEEP)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    return sh36735_write_register(driver, SH36735_REG_SCONF1, mode_value);
}

BmsStatus sh36735_clear_protection_flags(Sh36735Driver *driver,
                                          uint8_t flag1_mask,
                                          uint8_t flag2_mask)
{
    uint8_t configuration;
    BmsStatus status;

    if ((flag1_mask == 0u) && (flag2_mask == 0u)) {
        return BMS_STATUS_OK;
    }
    status = sh36735_read_registers(driver, SH36735_REG_SCONF2, &configuration, 1u);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    status = sh36735_write_register(driver, SH36735_REG_SCONF2,
                                     (uint8_t)(configuration | SH36735_SCONF2_LTCLR));
    if (status != BMS_STATUS_OK) {
        return status;
    }
    if (flag1_mask != 0u) {
        status = sh36735_write_register(driver, SH36735_REG_FLAG1, (uint8_t)~flag1_mask);
        if (status != BMS_STATUS_OK) {
            return status;
        }
    }
    if (flag2_mask != 0u) {
        return sh36735_write_register(driver, SH36735_REG_FLAG2, (uint8_t)~flag2_mask);
    }
    return BMS_STATUS_OK;
}
