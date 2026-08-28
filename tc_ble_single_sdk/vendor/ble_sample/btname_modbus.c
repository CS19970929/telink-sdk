#include "btname_modbus.h"

static char s_name[BTNAME_TOTAL_MAX_LEN] = "BT_d011_default";
static void (*s_refresh_cb)(void) = 0;

void btname_set_refresh_callback(void (*cb)(void))
{
    s_refresh_cb = cb;
}

void btname_init(void)
{
    s_name[BTNAME_TOTAL_MAX_LEN - 1u] = '\0';
}

const char *btname_get(void)
{
    return s_name;
}

int btname_modbus_on_write_holding(u16 addr, u16 qty, const u16 *regs)
{
    u16 i;
    u16 off;
    if (!regs || qty == 0u || addr < BTNAME_REG_BASE ||
        (u32)addr + qty > (u32)BTNAME_REG_BASE + BTNAME_REG_COUNT) {
        return 0;
    }

    for (i = 0; i < qty; ++i) {
        off = (u16)((addr - BTNAME_REG_BASE + i) * 2u);
        if (off < BTNAME_TOTAL_MAX_LEN - 1u)
            s_name[off] = (char)(regs[i] >> 8);
        if (off + 1u < BTNAME_TOTAL_MAX_LEN - 1u)
            s_name[off + 1u] = (char)(regs[i] & 0xFFu);
    }
    s_name[BTNAME_TOTAL_MAX_LEN - 1u] = '\0';

    /* The SDK's minimal common/string.c does not provide strncmp(). Keep this
     * small prefix check local instead of pulling in a host libc dependency.
     */
    if (s_name[0] != 'B' || s_name[1] != 'T' || s_name[2] != '_') {
        s_name[0] = 'B';
        s_name[1] = 'T';
        s_name[2] = '_';
    }
    if (s_refresh_cb) s_refresh_cb();
    return 1;
}
