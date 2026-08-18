#include "bms/bms_application.h"

static void bms_application_log_change(BmsApplication *application, uint32_t timestamp,
                                       uint8_t type, uint32_t before, uint32_t after)
{
    BmsEvent event;
    if (before == after) {
        return;
    }
    event.timestamp_ms = timestamp;
    event.type = type;
    event.severity = (after != 0u) ? 1u : 0u;
    event.before = before;
    event.after = after;
    bms_event_log_append(&application->events, &event);
}

void bms_application_init(BmsApplication *application, const BmsProductConfig *product)
{
    if ((application == 0) || (product == 0)) {
        return;
    }
    application->product = *product;
    bms_parameters_set_defaults(&application->parameters);
    bms_protection_init(&application->protection);
    bms_soc_init(&application->soc, &application->parameters);
    bms_balance_init(&application->balance);
    bms_heating_init(&application->heating);
    bms_event_log_init(&application->events);
    application->output.desired_power.charge_enabled = 0u;
    application->output.desired_power.discharge_enabled = 0u;
    application->output.desired_power.precharge_enabled = 0u;
    application->output.desired_balance_mask = 0u;
    application->output.heating_requested = 0u;
}

void bms_application_step(BmsApplication *application, BmsRealtime *realtime,
                          uint32_t elapsed_ms)
{
    BmsProtectionResult protection_result;
    uint32_t previous_protection;
    uint32_t previous_faults;
    if ((application == 0) || (realtime == 0)) {
        return;
    }
    previous_protection = realtime->protection_flags;
    previous_faults = realtime->fault_flags;
    bms_protection_evaluate(&application->protection, &application->product,
                            &application->parameters, realtime, elapsed_ms,
                            &protection_result);
    realtime->alarm_flags = protection_result.alarm_flags;
    realtime->protection_flags = protection_result.protection_flags;
    bms_soc_update(&application->soc, &application->parameters, realtime, elapsed_ms);
    realtime->soc_permil = application->soc.soc_permil;
    realtime->soh_permil = application->soc.soh_permil;
    application->output.desired_power = protection_result.power_command;
    application->output.desired_balance_mask = bms_balance_update(&application->balance,
        &application->parameters, realtime, realtime->protection_flags);
    application->output.heating_requested = bms_heating_update(&application->heating,
        &application->parameters, realtime, realtime->protection_flags);
    bms_application_log_change(application, realtime->timestamp_ms,
                               BMS_EVENT_PROTECTION_CHANGED, previous_protection,
                               realtime->protection_flags);
    bms_application_log_change(application, realtime->timestamp_ms,
                               BMS_EVENT_AFE_FAULT_CHANGED, previous_faults,
                               realtime->fault_flags);
}

BmsStatus bms_application_set_parameters(BmsApplication *application,
                                         const BmsParameterWrite *writes,
                                         uint8_t write_count,
                                         uint32_t timestamp_ms)
{
    BmsStatus status;
    if (application == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = bms_parameters_set_many(&application->parameters, writes, write_count);
    if (status == BMS_STATUS_OK) {
        bms_application_log_change(application, timestamp_ms, BMS_EVENT_PARAMETER_CHANGED,
                                   0u, (uint32_t)write_count);
    }
    return status;
}

BmsStatus bms_application_set_soc(BmsApplication *application, uint16_t soc_permil,
                                  uint32_t timestamp_ms)
{
    BmsStatus status;
    if (application == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    status = bms_soc_set(&application->soc, soc_permil);
    if (status == BMS_STATUS_OK) {
        bms_application_log_change(application, timestamp_ms, BMS_EVENT_SOC_SET, 0u,
                                   soc_permil);
    }
    return status;
}
