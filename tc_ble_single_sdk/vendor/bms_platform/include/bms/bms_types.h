#ifndef BMS_TYPES_H
#define BMS_TYPES_H

#include <stdint.h>

#define BMS_MAX_CELLS             (32u)
#define BMS_MAX_TEMPERATURES      (8u)
#define BMS_SOC_PERMIL_MAX        (1000u)

typedef enum {
    BMS_STATUS_OK = 0,
    BMS_STATUS_INVALID_ARGUMENT,
    BMS_STATUS_NOT_READY,
    BMS_STATUS_NOT_SUPPORTED,
    BMS_STATUS_IO_ERROR,
    BMS_STATUS_CRC_ERROR,
    BMS_STATUS_PROTOCOL_ERROR,
    BMS_STATUS_STATE_ERROR
} BmsStatus;

typedef enum {
    BMS_POWER_TOPOLOGY_SHARED_PORT = 0,
    BMS_POWER_TOPOLOGY_SEPARATE_PORT,
    BMS_POWER_TOPOLOGY_HIGH_SIDE,
    BMS_POWER_TOPOLOGY_LOW_SIDE
} BmsPowerTopology;

typedef struct {
    uint8_t charge_enabled;
    uint8_t discharge_enabled;
    uint8_t precharge_enabled;
} BmsPowerCommand;

typedef struct {
    uint8_t charge_enabled;
    uint8_t discharge_enabled;
    uint8_t precharge_enabled;
} BmsPowerState;

#endif /* BMS_TYPES_H */
