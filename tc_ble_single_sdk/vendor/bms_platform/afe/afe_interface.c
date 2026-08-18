#include "bms/afe/afe_interface.h"

static BmsStatus afe_validate(const AfeDevice *device)
{
    if ((device == 0) || (device->ops == 0) || (device->capabilities == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    return BMS_STATUS_OK;
}

BmsStatus afe_init(AfeDevice *device)
{
    BmsStatus status = afe_validate(device);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    if (device->ops->init == 0) {
        return BMS_STATUS_NOT_SUPPORTED;
    }
    return device->ops->init(device->context);
}

BmsStatus afe_read_measurement(AfeDevice *device, BmsMeasurement *measurement)
{
    BmsStatus status = afe_validate(device);
    if ((status != BMS_STATUS_OK) || (measurement == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (device->ops->read_measurement == 0) {
        return BMS_STATUS_NOT_SUPPORTED;
    }
    return device->ops->read_measurement(device->context, measurement);
}

BmsStatus afe_set_balance(AfeDevice *device, uint32_t cell_mask)
{
    BmsStatus status = afe_validate(device);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    if (device->ops->set_balance == 0) {
        return BMS_STATUS_NOT_SUPPORTED;
    }
    return device->ops->set_balance(device->context, cell_mask);
}

BmsStatus afe_set_power(AfeDevice *device, const BmsPowerCommand *command)
{
    BmsStatus status = afe_validate(device);
    if ((status != BMS_STATUS_OK) || (command == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (device->ops->set_power == 0) {
        return BMS_STATUS_NOT_SUPPORTED;
    }
    return device->ops->set_power(device->context, command);
}

BmsStatus afe_get_faults(AfeDevice *device, AfeFaultSnapshot *faults)
{
    BmsStatus status = afe_validate(device);
    if ((status != BMS_STATUS_OK) || (faults == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (device->ops->get_faults == 0) {
        return BMS_STATUS_NOT_SUPPORTED;
    }
    return device->ops->get_faults(device->context, faults);
}

BmsStatus afe_set_power_mode(AfeDevice *device, AfePowerMode mode)
{
    BmsStatus status = afe_validate(device);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    if (device->ops->set_power_mode == 0) {
        return BMS_STATUS_NOT_SUPPORTED;
    }
    return device->ops->set_power_mode(device->context, mode);
}
