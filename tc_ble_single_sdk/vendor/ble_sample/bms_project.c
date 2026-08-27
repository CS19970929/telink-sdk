#include "bms_project.h"
#include "hs_d011_board.h"
#include "modbus_uart.h"
#include "btname_modbus.h"
#include "bms_ble_compat.h"
#include <string.h>

static bms_project_state_t g_bms;
static u32 s_afe_poll_tick;

static const u16 k_default_protect[BMS_PROTECT_REG_COUNT] = {
    /* 0x2100: Vcell OVP: L1,L2,L3,recover,filter(ms) */
    4100, 4150, 4200, 4050, 100,
    /* Vcell UVP */
    3000, 2900, 2700, 3050, 100,
    /* Vbus OVP */
    4100, 4150, 4200, 4050, 100,
    /* Vbus UVP */
    3000, 2900, 2700, 3050, 100,
    /* charge OCP (legacy unit A*10), levels/recover/filter */
    100, 150, 200, 100, 500,
    /* discharge OCP */
    100, 150, 200, 100, 500,
    /* charge high temperature: (degC+40)*10 */
    800, 900, 950, 900, 100,
    /* charge low temperature */
    450, 430, 400, 450, 100,
    /* discharge high temperature */
    900, 950, 1000, 900, 100,
    /* discharge low temperature */
    300, 250, 200, 300, 100,
    /* MOS high temperature */
    1150, 1250, 1350, 1200, 100,
    /* cell delta */
    600, 800, 1000, 800, 100,
    /* SOC low */
    20, 10, 5, 11, 100
};

static void board_gpio_init(void)
{
    const GPIO_PinTypeDef outputs[] = {
        BMS_HEAT_CHG_PIN, BMS_HEAT_RF_EN_PIN, BMS_LED_PIN, BMS_CMNT_EN_PIN
    };
    u8 i;
    for (i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        gpio_set_func(outputs[i], AS_GPIO);
        gpio_set_input_en(outputs[i], 0);
        gpio_set_output_en(outputs[i], 1);
        gpio_write(outputs[i], 0);
    }

    gpio_set_func(BMS_DI1_PIN, AS_GPIO);
    gpio_set_input_en(BMS_DI1_PIN, 1);
    gpio_set_output_en(BMS_DI1_PIN, 0);

    gpio_set_func(BMS_INT_WAKE_MCU_PIN, AS_GPIO);
    gpio_set_input_en(BMS_INT_WAKE_MCU_PIN, 1);
    gpio_set_output_en(BMS_INT_WAKE_MCU_PIN, 0);

    gpio_set_func(BMS_CMNT_WAKE_PIN, AS_GPIO);
    gpio_set_input_en(BMS_CMNT_WAKE_PIN, 1);
    gpio_set_output_en(BMS_CMNT_WAKE_PIN, 0);
}

void bms_project_init(void)
{
    int rc;
    memset(&g_bms, 0, sizeof(g_bms));
    memcpy(g_bms.protect, k_default_protect, sizeof(g_bms.protect));
    g_bms.soh = 100u;

    board_gpio_init();
    btname_init();
    btname_set_refresh_callback(bms_ble_refresh_name);
    bms_ble_compat_apply();

    rc = sh3673510_init();
    g_bms.afe_last_error = (s16)rc;
    g_bms.afe_init_ok = (rc == 0) ? 1u : 0u;
    if (g_bms.afe_init_ok) {
        /* Use the third-level legacy values as the AFE hard protection limits. */
        if (sh3673510_set_ov_mv(g_bms.protect[2]) != 0 ||
            sh3673510_set_uv_mv(g_bms.protect[7]) != 0) {
            g_bms.afe_init_ok = 0u;
            g_bms.afe_last_error = -20;
        }
    }

    modbus_uart_init();
    s_afe_poll_tick = clock_time();
}

void bms_project_process(void)
{
    int rc;
    modbus_uart_process();

    if (clock_time_exceed(s_afe_poll_tick, BMS_AFE_POLL_PERIOD_US)) {
        s_afe_poll_tick = clock_time();
        rc = sh3673510_sample(&g_bms.afe);
        if (rc != 0) {
            g_bms.afe.online = 0u;
            g_bms.afe_last_error = (s16)rc;
        } else {
            g_bms.afe_last_error = 0;
        }
    }
}

void bms_project_irq_handler(void)
{
    modbus_uart_irq_proc();
}

const bms_project_state_t *bms_project_get_state(void)
{
    return &g_bms;
}

static u16 legacy_temp(u8 index)
{
    s16 t;
    if (index >= 4u) return 0u;
    t = g_bms.afe.temp_dC[index] + 400;
    if (t < 0) t = 0;
    return (u16)t;
}

static u16 legacy_fault_word(void)
{
    u16 f = 0;
    if (g_bms.afe.flag1 & BIT(0)) f |= BIT(0);  /* cell OVP */
    if (g_bms.afe.flag1 & BIT(1)) f |= BIT(1);  /* cell UVP */
    if (g_bms.afe.flag1 & BIT(5)) f |= BIT(4);  /* OCC */
    if (g_bms.afe.flag1 & (BIT(2) | BIT(3) | BIT(4))) f |= BIT(5); /* OCD/SC */
    if (g_bms.afe.flag2 & BIT(3)) f |= BIT(6);  /* charge OTP */
    if (g_bms.afe.flag2 & BIT(7)) f |= BIT(7);  /* discharge OTP */
    if (g_bms.afe.flag2 & BIT(4)) f |= BIT(8);  /* charge UTP */
    if (g_bms.afe.flag2 & BIT(6)) f |= BIT(9);  /* discharge UTP */
    return f;
}

u16 bms_project_read_legacy_d000(u16 offset)
{
    s32 ma;
    if (offset < 32u)
        return (offset < BMS_CELL_COUNT) ? g_bms.afe.cell_mv[offset] : 0u;

    switch (offset) {
    case 32: return g_bms.afe.cell_max_mv;
    case 33: return g_bms.afe.cell_min_mv;
    case 34: {
        u8 i; for (i = 0; i < BMS_CELL_COUNT; ++i) if (g_bms.afe.cell_mv[i] == g_bms.afe.cell_max_mv) return (u16)(i + 1u);
        return 0;
    }
    case 35: {
        u8 i; for (i = 0; i < BMS_CELL_COUNT; ++i) if (g_bms.afe.cell_mv[i] == g_bms.afe.cell_min_mv) return (u16)(i + 1u);
        return 0;
    }
    case 36: return g_bms.afe.cell_delta_mv;
    case 37: return (u16)(g_bms.afe.pack_mv / 10u); /* V*100 */
    case 38: return legacy_temp(0);
    case 39: return legacy_temp(1);
    case 40: return legacy_temp(2);
    case 41: return legacy_temp(3); /* TS4-MOS */
    case 42: case 43: case 44: case 45: case 46: case 47: return 0u;
    case 48: {
        u16 a = legacy_temp(0), b = legacy_temp(1), c = legacy_temp(3);
        u16 m = a > b ? a : b; return m > c ? m : c;
    }
    case 49: {
        u16 a = legacy_temp(0), b = legacy_temp(1), c = legacy_temp(3);
        u16 m = a < b ? a : b; return m < c ? m : c;
    }
    case 50:
        ma = g_bms.afe.current_ma_valid ? g_bms.afe.current_ma : 0;
        return (ma < 0) ? (u16)((-ma) / 100) : 0u; /* charge A*10 */
    case 51:
        ma = g_bms.afe.current_ma_valid ? g_bms.afe.current_ma : 0;
        return (ma > 0) ? (u16)(ma / 100) : 0u; /* discharge A*10 */
    case 52: return g_bms.soc;
    case 53: return g_bms.soh;
    case 54: return g_bms.capacity_now_x100;
    case 55: return g_bms.capacity_full_x100;
    case 56: return g_bms.capacity_factory_x100;
    case 57: return g_bms.cycle_count;
    case 58: return legacy_fault_word();
    case 59: return legacy_fault_word();
    case 60: return legacy_fault_word();
    case 61: return 0u;
    case 62: return 0u;
    default: return 0u;
    }
}

u16 bms_project_read_protect(u16 offset)
{
    return (offset < BMS_PROTECT_REG_COUNT) ? g_bms.protect[offset] : 0u;
}

int bms_project_write_protect(u16 offset, u16 value)
{
    if (offset >= BMS_PROTECT_REG_COUNT) return 0;
    g_bms.protect[offset] = value;

    /* Preserve the legacy table, but only write fields that map unambiguously to SH3673510. */
    if (offset == 2u) return sh3673510_set_ov_mv(value) == 0;
    if (offset == 7u) return sh3673510_set_uv_mv(value) == 0;
    return 1;
}

void bms_project_set_soc(u16 soc)
{
    g_bms.soc = (soc > 100u) ? 100u : soc;
}

int bms_project_command(u16 value)
{
    switch (value) {
    case 0x0010u: /* explicit clear of all active AFE protection flags */
        return sh3673510_clear_flags(0x3Fu, 0xF8u) == 0;
    case 0x0011u: /* explicit charge/discharge enable request */
        return sh3673510_set_mos(1u, 1u) == 0;
    case 0x0012u: /* explicit MOS off request */
        return sh3673510_set_mos(0u, 0u) == 0;
    default:
        return 1; /* old commands not applicable to this hardware are accepted as no-op */
    }
}
