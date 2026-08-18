#include "bms/afe/sh36735_driver.h"

#define SH36735_COMMAND_WRITE          (0x01u)
#define SH36735_COMMAND_READ           (0x02u)
#define SH36735_COMMAND_SOFTWARE_RESET (0x0bu)
#define SH36735_SPI_IDLE_BYTE          (0x00u)
#define SH36735_SPI_READ_INITIAL_BYTE  (0xffu)
#define SH36735_SPI_ACK                (0xa5u)

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
