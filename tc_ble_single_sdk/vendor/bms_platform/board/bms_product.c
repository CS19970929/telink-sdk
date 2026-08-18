#include "bms/bms_product.h"

static const BmsProductConfig g_bms_default_product = {
    BMS_AFE_KIND_SH36735,
    20u,
    4u,
    BMS_POWER_TOPOLOGY_SHARED_PORT
};

const BmsProductConfig *bms_product_default_config(void)
{
    return &g_bms_default_product;
}

BmsStatus bms_product_validate(const BmsProductConfig *config)
{
    if (config == 0) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    if ((config->cell_count == 0u) || (config->cell_count > BMS_MAX_CELLS)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    if (config->temperature_count > BMS_MAX_TEMPERATURES) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    if (config->power_topology > BMS_POWER_TOPOLOGY_LOW_SIDE) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }

    return BMS_STATUS_OK;
}
