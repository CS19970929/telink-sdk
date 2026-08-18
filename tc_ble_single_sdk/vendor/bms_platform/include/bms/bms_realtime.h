#ifndef BMS_REALTIME_H
#define BMS_REALTIME_H

#include "bms/bms_product.h"

#define BMS_MEASUREMENT_VALID_CELLS        (1u << 0)
#define BMS_MEASUREMENT_VALID_TEMPERATURES (1u << 1)
#define BMS_MEASUREMENT_VALID_PACK_VOLTAGE (1u << 2)
#define BMS_MEASUREMENT_VALID_CURRENT      (1u << 3)
#define BMS_MEASUREMENT_VALID_CHARGER      (1u << 4)
#define BMS_MEASUREMENT_VALID_LOAD         (1u << 5)

/* All voltages use mV, current uses mA and temperatures use 0.1 degree C. */
typedef struct {
    uint32_t timestamp_ms;
    uint32_t valid_flags;
    uint8_t cell_count;
    uint8_t temperature_count;
    uint16_t cell_voltage_mv[BMS_MAX_CELLS];
    int16_t temperature_decic[BMS_MAX_TEMPERATURES];
    uint32_t pack_voltage_mv;
    int32_t current_ma;
    uint8_t charger_present;
    uint8_t load_present;
} BmsMeasurement;

typedef struct {
    uint32_t sample_sequence;
    uint32_t timestamp_ms;
    uint32_t valid_flags;
    uint8_t cell_count;
    uint8_t temperature_count;
    uint16_t cell_voltage_mv[BMS_MAX_CELLS];
    int16_t temperature_decic[BMS_MAX_TEMPERATURES];
    uint32_t pack_voltage_mv;
    int32_t current_ma;
    int32_t power_mw;
    uint16_t soc_permil;
    uint16_t soh_permil;
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_delta_mv;
    uint32_t balance_cells_mask;
    BmsPowerState power_state;
    uint32_t alarm_flags;
    uint32_t protection_flags;
    uint32_t fault_flags;
    uint8_t charger_present;
    uint8_t load_present;
    uint8_t heating_requested;
} BmsRealtime;

void bms_realtime_init(BmsRealtime *realtime, const BmsProductConfig *config);
BmsStatus bms_realtime_publish_measurement(BmsRealtime *realtime,
                                           const BmsMeasurement *measurement);

#endif /* BMS_REALTIME_H */
