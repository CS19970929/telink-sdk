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

    if (adapter->configure_cell_count_on_init != 0u) {
        return sh36735_set_series_cell_count(adapter->driver, adapter->cell_count);
    }
    return sh36735_read_registers(adapter->driver, SH36735_REG_SCONF1,
                                  &system_configuration, 1u);
}

static BmsStatus sh36735_adapter_read_measurement(void *context,
                                                   BmsMeasurement *measurement)
{
    Sh36735Adapter *adapter = (Sh36735Adapter *)context;
    Sh36735RawSnapshot snapshot;
    uint8_t index;
    BmsStatus status;

    if ((adapter == 0) || (adapter->driver == 0) || (measurement == 0) ||
        (adapter->cell_voltage_from_code == 0) ||
        ((adapter->temperature_count != 0u) && (adapter->temperature_from_code == 0))) {
        return BMS_STATUS_NOT_READY;
    }

    status = sh36735_read_raw_snapshot(adapter->driver, adapter->cell_count,
                                        adapter->temperature_count, &snapshot);
    if (status != BMS_STATUS_OK) {
        return status;
    }

    measurement->valid_flags = 0u;
    measurement->cell_count = adapter->cell_count;
    measurement->temperature_count = adapter->temperature_count;
    measurement->pack_voltage_mv = 0u;
    measurement->current_ma = 0;
    measurement->charger_present = 0u;
    measurement->load_present = 0u;
    for (index = 0u; index < adapter->cell_count; ++index) {
        status = adapter->cell_voltage_from_code(adapter->conversion_context, index,
                                                 snapshot.cell_code[index],
                                                 &measurement->cell_voltage_mv[index]);
        if (status != BMS_STATUS_OK) {
            return status;
        }
        measurement->pack_voltage_mv += measurement->cell_voltage_mv[index];
    }
    measurement->valid_flags |= BMS_MEASUREMENT_VALID_CELLS |
                                BMS_MEASUREMENT_VALID_PACK_VOLTAGE;
    for (index = 0u; index < adapter->temperature_count; ++index) {
        status = adapter->temperature_from_code(adapter->conversion_context, index,
                                                snapshot.temperature_code[index],
                                                &measurement->temperature_decic[index]);
        if (status != BMS_STATUS_OK) {
            return status;
        }
    }
    if (adapter->temperature_count != 0u) {
        measurement->valid_flags |= BMS_MEASUREMENT_VALID_TEMPERATURES;
    }
    if (adapter->current_from_code != 0) {
        status = adapter->current_from_code(adapter->conversion_context, snapshot.current_code,
                                            &measurement->current_ma);
        if (status != BMS_STATUS_OK) {
            return status;
        }
        measurement->valid_flags |= BMS_MEASUREMENT_VALID_CURRENT;
    }
    if (adapter->charger_from_code != 0) {
        status = adapter->charger_from_code(adapter->conversion_context,
                                            snapshot.charger_voltage_code,
                                            snapshot.bstatus2,
                                            &measurement->charger_present);
        if ((status != BMS_STATUS_OK) || (measurement->charger_present > 1u)) {
            return (status == BMS_STATUS_OK) ? BMS_STATUS_PROTOCOL_ERROR : status;
        }
        measurement->valid_flags |= BMS_MEASUREMENT_VALID_CHARGER;
    }
    if (adapter->load_from_status != 0) {
        status = adapter->load_from_status(adapter->conversion_context, snapshot.bstatus2,
                                           &measurement->load_present);
        if ((status != BMS_STATUS_OK) || (measurement->load_present > 1u)) {
            return (status == BMS_STATUS_OK) ? BMS_STATUS_PROTOCOL_ERROR : status;
        }
        measurement->valid_flags |= BMS_MEASUREMENT_VALID_LOAD;
    }
    return BMS_STATUS_OK;
}

static BmsStatus sh36735_adapter_set_balance(void *context, uint32_t cell_mask)
{
    Sh36735Adapter *adapter = (Sh36735Adapter *)context;
    uint32_t invalid_mask;

    if ((adapter == 0) || (adapter->driver == 0) || (adapter->allow_balance_control == 0u)) {
        return BMS_STATUS_NOT_READY;
    }
    invalid_mask = ~(((uint32_t)1u << adapter->cell_count) - 1u);
    if ((cell_mask & invalid_mask) != 0u) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    return sh36735_set_balance_mask(adapter->driver, cell_mask);
}

static BmsStatus sh36735_adapter_set_power(void *context,
                                           const BmsPowerCommand *command)
{
    Sh36735Adapter *adapter = (Sh36735Adapter *)context;
    if ((adapter == 0) || (adapter->driver == 0) || (command == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->allow_power_control == 0u) {
        return BMS_STATUS_NOT_READY;
    }
    return sh36735_set_power_command(adapter->driver, command);
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
    Sh36735Adapter *adapter = (Sh36735Adapter *)context;
    uint8_t mode_value;

    if ((adapter == 0) || (adapter->driver == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->allow_power_control == 0u) {
        return BMS_STATUS_NOT_READY;
    }
    if (mode == AFE_POWER_MODE_NORMAL) {
        mode_value = SH36735_SCONF1_NORMAL;
    } else if (mode == AFE_POWER_MODE_IDLE) {
        mode_value = SH36735_SCONF1_IDLE;
    } else if (mode == AFE_POWER_MODE_SLEEP) {
        mode_value = SH36735_SCONF1_SLEEP;
    } else {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    return sh36735_set_power_mode(adapter->driver, mode_value);
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
