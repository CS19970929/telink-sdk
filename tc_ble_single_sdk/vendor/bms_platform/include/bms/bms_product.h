#ifndef BMS_PRODUCT_H
#define BMS_PRODUCT_H

#include "bms/bms_types.h"

typedef enum {
    BMS_AFE_KIND_UNKNOWN = 0,
    BMS_AFE_KIND_SH36735
} BmsAfeKind;

typedef struct {
    BmsAfeKind afe_kind;
    uint8_t cell_count;
    uint8_t temperature_count;
    BmsPowerTopology power_topology;
} BmsProductConfig;

const BmsProductConfig *bms_product_default_config(void);
BmsStatus bms_product_validate(const BmsProductConfig *config);

#endif /* BMS_PRODUCT_H */
