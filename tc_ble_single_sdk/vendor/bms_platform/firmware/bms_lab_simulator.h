#ifndef BMS_LAB_SIMULATOR_H
#define BMS_LAB_SIMULATOR_H

#include "bms/bms_types.h"

/* Publishes deterministic BMS measurements; it never touches AFE or GPIO. */
void bms_lab_simulator_init(void);
void bms_lab_simulator_process(uint32_t timestamp_ms);

#endif /* BMS_LAB_SIMULATOR_H */
