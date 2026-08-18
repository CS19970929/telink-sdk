#include "bms/bms_platform.h"

static void bms_platform_record_afe_failure(BmsPlatform *platform,
                                            BmsStatus status)
{
    if (status != BMS_STATUS_NOT_READY) {
        platform->state = BMS_PLATFORM_STATE_AFE_ERROR;
    }
}

BmsStatus bms_platform_init(BmsPlatform *platform,
                            const BmsProductConfig *product,
                            const AfeDevice *afe)
{
    BmsStatus status;

    if ((platform == 0) || (afe == 0)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    status = bms_product_validate(product);
    if (status != BMS_STATUS_OK) {
        return status;
    }

    if ((afe->capabilities == 0) ||
        (product->cell_count > afe->capabilities->max_cell_count) ||
        (product->temperature_count > afe->capabilities->max_temperature_count)) {
        return BMS_STATUS_NOT_SUPPORTED;
    }

    platform->state = BMS_PLATFORM_STATE_RESET;
    platform->product = *product;
    platform->afe = *afe;
    bms_realtime_init(&platform->realtime, product);
    bms_application_init(&platform->application, product);
    platform->afe_faults.common_flags = 0u;
    platform->afe_faults.vendor_status = 0u;

    status = afe_init(&platform->afe);
    if (status != BMS_STATUS_OK) {
        platform->state = BMS_PLATFORM_STATE_AFE_ERROR;
        return status;
    }

    platform->state = BMS_PLATFORM_STATE_READY;
    return BMS_STATUS_OK;
}

BmsStatus bms_platform_poll(BmsPlatform *platform, uint32_t timestamp_ms)
{
    BmsMeasurement measurement;
    BmsStatus status;
    uint32_t elapsed_ms;

    if (platform == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (platform->state != BMS_PLATFORM_STATE_READY) {
        return BMS_STATUS_STATE_ERROR;
    }

    status = afe_read_measurement(&platform->afe, &measurement);
    if (status != BMS_STATUS_OK) {
        bms_platform_record_afe_failure(platform, status);
        return status;
    }
    if ((measurement.cell_count != platform->product.cell_count) ||
        (measurement.temperature_count != platform->product.temperature_count)) {
        platform->state = BMS_PLATFORM_STATE_AFE_ERROR;
        return BMS_STATUS_PROTOCOL_ERROR;
    }
    elapsed_ms = (platform->realtime.sample_sequence == 0u) ? 0u :
                 (uint32_t)(timestamp_ms - platform->realtime.timestamp_ms);
    measurement.timestamp_ms = timestamp_ms;

    status = bms_realtime_publish_measurement(&platform->realtime, &measurement);
    if (status != BMS_STATUS_OK) {
        return status;
    }

    status = afe_get_faults(&platform->afe, &platform->afe_faults);
    if (status != BMS_STATUS_OK) {
        bms_platform_record_afe_failure(platform, status);
        return status;
    }

    platform->realtime.fault_flags = platform->afe_faults.common_flags;
    bms_application_step(&platform->application, &platform->realtime, elapsed_ms);
    platform->realtime.heating_requested = platform->application.output.heating_requested;

    status = afe_set_power(&platform->afe, &platform->application.output.desired_power);
    if (status == BMS_STATUS_OK) {
        platform->realtime.power_state.charge_enabled =
            platform->application.output.desired_power.charge_enabled;
        platform->realtime.power_state.discharge_enabled =
            platform->application.output.desired_power.discharge_enabled;
        platform->realtime.power_state.precharge_enabled =
            platform->application.output.desired_power.precharge_enabled;
    } else if ((status != BMS_STATUS_NOT_READY) && (status != BMS_STATUS_NOT_SUPPORTED)) {
        bms_platform_record_afe_failure(platform, status);
        return status;
    }
    status = afe_set_balance(&platform->afe, platform->application.output.desired_balance_mask);
    if (status == BMS_STATUS_OK) {
        platform->realtime.balance_cells_mask = platform->application.output.desired_balance_mask;
    } else if ((status != BMS_STATUS_NOT_READY) && (status != BMS_STATUS_NOT_SUPPORTED)) {
        bms_platform_record_afe_failure(platform, status);
        return status;
    }
    return BMS_STATUS_OK;
}

BmsStatus bms_platform_set_parameters(BmsPlatform *platform,
                                      const BmsParameterWrite *writes,
                                      uint8_t write_count)
{
    if (platform == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    return bms_application_set_parameters(&platform->application, writes, write_count,
                                          platform->realtime.timestamp_ms);
}
