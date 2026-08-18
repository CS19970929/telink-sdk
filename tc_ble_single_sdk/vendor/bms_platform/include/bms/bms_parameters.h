#ifndef BMS_PARAMETERS_H
#define BMS_PARAMETERS_H

#include "bms/bms_types.h"

/* Parameter IDs are protocol-stable BMS semantics, never AFE registers. */
typedef enum {
    BMS_PARAM_CELL_OV_TRIP_MV = 0x0101,
    BMS_PARAM_CELL_OV_RELEASE_MV = 0x0102,
    BMS_PARAM_CELL_UV_TRIP_MV = 0x0103,
    BMS_PARAM_CELL_UV_RELEASE_MV = 0x0104,
    BMS_PARAM_PROTECTION_DELAY_MS = 0x0105,
    BMS_PARAM_CHARGE_OC_TRIP_MA = 0x0110,
    BMS_PARAM_DISCHARGE_OC_TRIP_MA = 0x0111,
    BMS_PARAM_CHARGE_TEMP_LOW_DECIC = 0x0120,
    BMS_PARAM_CHARGE_TEMP_HIGH_DECIC = 0x0121,
    BMS_PARAM_DISCHARGE_TEMP_LOW_DECIC = 0x0122,
    BMS_PARAM_DISCHARGE_TEMP_HIGH_DECIC = 0x0123,
    BMS_PARAM_CELL_DELTA_ALARM_MV = 0x0130,
    BMS_PARAM_NOMINAL_CAPACITY_MAH = 0x0201,
    BMS_PARAM_SOC_INITIAL_PERMIL = 0x0202,
    BMS_PARAM_BALANCE_START_MV = 0x0301,
    BMS_PARAM_BALANCE_DELTA_MV = 0x0302,
    BMS_PARAM_BALANCE_MIN_TEMP_DECIC = 0x0303,
    BMS_PARAM_BALANCE_MAX_TEMP_DECIC = 0x0304,
    BMS_PARAM_BALANCE_MAX_CURRENT_MA = 0x0305,
    BMS_PARAM_HEATING_START_DECIC = 0x0401,
    BMS_PARAM_HEATING_STOP_DECIC = 0x0402
} BmsParameterId;

typedef enum {
    BMS_PARAMETER_TYPE_UNSIGNED = 0,
    BMS_PARAMETER_TYPE_SIGNED = 1
} BmsParameterType;

#define BMS_PARAMETER_FLAG_READ       (1u << 0)
#define BMS_PARAMETER_FLAG_WRITE      (1u << 1)
#define BMS_PARAMETER_FLAG_PERSISTENT (1u << 2)

typedef struct {
    uint16_t id;
    uint8_t type;
    uint8_t flags;
    int32_t minimum;
    int32_t maximum;
    int32_t default_value;
} BmsParameterDescriptor;

typedef struct {
    uint16_t id;
    int32_t value;
} BmsParameterWrite;

typedef struct {
    uint16_t cell_ov_trip_mv;
    uint16_t cell_ov_release_mv;
    uint16_t cell_uv_trip_mv;
    uint16_t cell_uv_release_mv;
    uint16_t protection_delay_ms;
    uint32_t charge_oc_trip_ma;
    uint32_t discharge_oc_trip_ma;
    int16_t charge_temp_low_decic;
    int16_t charge_temp_high_decic;
    int16_t discharge_temp_low_decic;
    int16_t discharge_temp_high_decic;
    uint16_t cell_delta_alarm_mv;
    uint16_t nominal_capacity_mah;
    uint16_t soc_initial_permil;
    uint16_t balance_start_mv;
    uint16_t balance_delta_mv;
    int16_t balance_min_temp_decic;
    int16_t balance_max_temp_decic;
    uint32_t balance_max_current_ma;
    int16_t heating_start_decic;
    int16_t heating_stop_decic;
} BmsParameters;

const BmsParameterDescriptor *bms_parameters_descriptors(uint8_t *count);
const BmsParameterDescriptor *bms_parameters_find(uint16_t id);
void bms_parameters_set_defaults(BmsParameters *parameters);
BmsStatus bms_parameters_get(const BmsParameters *parameters,
                              uint16_t id,
                              int32_t *value);
BmsStatus bms_parameters_set(BmsParameters *parameters,
                              uint16_t id,
                              int32_t value);
BmsStatus bms_parameters_set_many(BmsParameters *parameters,
                                   const BmsParameterWrite *writes,
                                   uint8_t write_count);
BmsStatus bms_parameters_validate(const BmsParameters *parameters);

#endif /* BMS_PARAMETERS_H */
