#include "bms/afe/sh36735_adapter.h"

static const AfeCapabilities g_sh36735_capabilities = {
    SH36735_MAX_CELLS,
    SH36735_MAX_TEMPERATURES,
    AFE_CAPABILITY_HARDWARE_BALANCE |
    AFE_CAPABILITY_CHARGE_SWITCH |
    AFE_CAPABILITY_DISCHARGE_SWITCH |
    AFE_CAPABILITY_PRECHARGE_SWITCH |
    AFE_CAPABILITY_HARDWARE_PROTECTION |
    AFE_CAPABILITY_COULOMB_COUNTER |
    AFE_CAPABILITY_CHARGER_DETECT |
    AFE_CAPABILITY_LOAD_DETECT |
    AFE_CAPABILITY_SLEEP
};

static BmsStatus sh36735_adapter_init(void *context)
{
    Sh36735Adapter *adapter = (Sh36735Adapter *)context;
    uint8_t system_configuration;

    if ((adapter == 0) || (adapter->driver == 0) ||
        (adapter->cell_count < 4u) || (adapter->cell_count > SH36735_MAX_CELLS) ||
        (adapter->temperature_count > SH36735_MAX_TEMPERATURES)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    return sh36735_read_registers(adapter->driver, SH36735_REG_SCONF1,
                                  &system_configuration, 1u);
}

static BmsStatus sh36735_adapter_read_measurement(void *context,
                                                   BmsMeasurement *measurement)
{
    (void)context;
    (void)measurement;

    /*
     * The SPI register layer is available, but engineering conversion from
     * VADC/CADC code to physical values needs the selected RSENSE and NTC
     * curve. Do not publish guessed values into BmsRealtime.
     */
    return BMS_STATUS_NOT_READY;
}

static BmsStatus sh36735_adapter_set_balance(void *context, uint32_t cell_mask)
{
    (void)context;
    (void)cell_mask;

    /*
     * Balance duration, thermal constraints and the chip's 30.38 s automatic
     * stop must be coordinated by the future Balance service.
     */
    return BMS_STATUS_NOT_READY;
}

static BmsStatus sh36735_adapter_set_power(void *context,
                                           const BmsPowerCommand *command)
{
    (void)context;
    (void)command;

    /*
     * Shared-port FET truth tables depend on the board-level high/low-side
     * MOS topology, which is intentionally not encoded in the common AFE API.
     */
    return BMS_STATUS_NOT_READY;
}

static BmsStatus sh36735_adapter_get_faults(void *context,
                                             AfeFaultSnapshot *faults)
{
    Sh36735Adapter *adapter = (Sh36735Adapter *)context;
    uint8_t registers[5];
    BmsStatus status;

    if ((adapter == 0) || (adapter->driver == 0) || (faults == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    status = sh36735_read_registers(adapter->driver, SH36735_REG_FLAG1,
                                    registers, 5u);
    if (status != BMS_STATUS_OK) {
        return status;
    }

    faults->common_flags = 0u;
    if ((registers[0] & (1u << 0)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_CELL_OV;
    }
    if ((registers[0] & (1u << 1)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_CELL_UV;
    }
    if ((registers[0] & (1u << 2)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_DISCHARGE_OC;
    }
    if ((registers[0] & (1u << 3)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_DISCHARGE_OC;
    }
    if ((registers[0] & (1u << 4)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_SHORT_CIRCUIT;
    }
    if ((registers[0] & (1u << 5)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_CHARGE_OC;
    }
    if ((registers[1] & ((1u << 0) | (1u << 1))) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_DISCHARGE_TEMP;
    }
    if ((registers[1] & ((1u << 2) | (1u << 3))) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_CHARGE_TEMP;
    }
    if ((registers[1] & (1u << 5)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_WATCHDOG;
    }
    if ((registers[1] & (1u << 4)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_COMMUNICATION;
    }
    if ((registers[2] & (1u << 0)) != 0u) {
        faults->common_flags |= AFE_HARDWARE_FAULT_OPEN_WIRE;
    }
    faults->vendor_status = ((uint32_t)registers[0]) |
                            ((uint32_t)registers[1] << 8) |
                            ((uint32_t)registers[2] << 16) |
                            ((uint32_t)registers[3] << 24);
    return BMS_STATUS_OK;
}

static BmsStatus sh36735_adapter_set_power_mode(void *context, AfePowerMode mode)
{
    (void)context;
    (void)mode;
    return BMS_STATUS_NOT_READY;
}

static const AfeOps g_sh36735_ops = {
    sh36735_adapter_init,
    sh36735_adapter_read_measurement,
    sh36735_adapter_set_balance,
    sh36735_adapter_set_power,
    sh36735_adapter_get_faults,
    sh36735_adapter_set_power_mode
};

BmsStatus sh36735_adapter_bind(Sh36735Adapter *adapter, AfeDevice *device)
{
    if ((adapter == 0) || (device == 0) || (adapter->driver == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    device->ops = &g_sh36735_ops;
    device->capabilities = &g_sh36735_capabilities;
    device->context = adapter;
    return BMS_STATUS_OK;
}
