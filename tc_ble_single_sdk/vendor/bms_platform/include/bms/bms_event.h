#ifndef BMS_EVENT_H
#define BMS_EVENT_H

#include "bms/bms_types.h"

#define BMS_EVENT_LOG_CAPACITY (32u)

typedef enum {
    BMS_EVENT_PROTECTION_CHANGED = 1,
    BMS_EVENT_PARAMETER_CHANGED = 2,
    BMS_EVENT_AFE_FAULT_CHANGED = 3,
    BMS_EVENT_SOC_SET = 4
} BmsEventType;

typedef struct {
    uint32_t timestamp_ms;
    uint8_t type;
    uint8_t severity;
    uint32_t before;
    uint32_t after;
} BmsEvent;

typedef struct {
    BmsEvent entries[BMS_EVENT_LOG_CAPACITY];
    uint8_t first;
    uint8_t count;
} BmsEventLog;

void bms_event_log_init(BmsEventLog *log);
void bms_event_log_append(BmsEventLog *log, const BmsEvent *event);
uint8_t bms_event_log_count(const BmsEventLog *log);
BmsStatus bms_event_log_get(const BmsEventLog *log, uint8_t index, BmsEvent *event);

#endif /* BMS_EVENT_H */
