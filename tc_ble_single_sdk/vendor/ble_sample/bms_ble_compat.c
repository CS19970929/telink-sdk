#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app_att.h"
#include "btname_modbus.h"
#include "bms_ble_compat.h"
#include <string.h>

extern own_addr_type_t app_own_address_type;

static u8 s_adv[] = {
    0x02, 0x01, 0x05,
    0x03, 0x19, 0x80, 0x01,
    0x05, 0x02, 0x12, 0x18, 0x0F, 0x18
};
static u8 s_scan[31];
static u8 s_scan_len;

void bms_ble_refresh_name(void)
{
    const char *name = btname_get();
    u8 n = (u8)strlen(name);
    if (n > 25u) n = 25u;

    memset(my_devName, 0, BTNAME_TOTAL_MAX_LEN);
    memcpy(my_devName, name, n);

    s_scan[0] = (u8)(n + 1u);
    s_scan[1] = 0x09u;
    memcpy(&s_scan[2], name, n);
    s_scan_len = (u8)(n + 2u);
    bls_ll_setScanRspData(s_scan, s_scan_len);
}

void bms_ble_compat_apply(void)
{
    /* Values intentionally match the established Telink BMS branch. */
    (void)bls_ll_setAdvParam(ADV_INTERVAL_800MS, ADV_INTERVAL_800MS,
                             ADV_TYPE_CONNECTABLE_UNDIRECTED,
                             app_own_address_type, 0, 0,
                             BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
    bls_ll_setAdvData(s_adv, sizeof(s_adv));
    bms_ble_refresh_name();
    rf_set_power_level_index(RF_POWER_P3dBm);
    bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}
