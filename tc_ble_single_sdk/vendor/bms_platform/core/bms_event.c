#include "bms/bms_event.h"

void bms_event_log_init(BmsEventLog *log)
{
    uint8_t index;
    if (log == 0) {
        return;
    }
    log->first = 0u;
    log->count = 0u;
    for (index = 0u; index < BMS_EVENT_LOG_CAPACITY; ++index) {
        log->entries[index].timestamp_ms = 0u;
        log->entries[index].type = 0u;
        log->entries[index].severity = 0u;
        log->entries[index].before = 0u;
        log->entries[index].after = 0u;
    }
}

void bms_event_log_append(BmsEventLog *log, const BmsEvent *event)
{
    uint8_t target;
    if ((log == 0) || (event == 0)) {
        return;
    }
    if (log->count < BMS_EVENT_LOG_CAPACITY) {
        target = (uint8_t)((log->first + log->count) % BMS_EVENT_LOG_CAPACITY);
        log->count++;
    } else {
        target = log->first;
        log->first = (uint8_t)((log->first + 1u) % BMS_EVENT_LOG_CAPACITY);
    }
    log->entries[target] = *event;
}

uint8_t bms_event_log_count(const BmsEventLog *log)
{
    return (log == 0) ? 0u : log->count;
}

BmsStatus bms_event_log_get(const BmsEventLog *log, uint8_t index, BmsEvent *event)
{
    if ((log == 0) || (event == 0) || (index >= log->count)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    *event = log->entries[(uint8_t)((log->first + index) % BMS_EVENT_LOG_CAPACITY)];
    return BMS_STATUS_OK;
}
