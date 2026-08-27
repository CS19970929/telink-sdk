#include "bms_project.h"
#include "hs_d011_board.h"
#include "modbus_uart.h"
#include "btname_modbus.h"
#include "bms_ble_compat.h"
#include "bms_protect.h"
#include "app_config.h"
#include "stack/ble/ble.h"
#include <string.h>

static bms_project_state_t g_bms;
static u32 s_afe_poll_tick;
static u8 s_afe_fail_count;

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

static int afe_start(void)
{
    int rc = bms_afe_init();
    if (rc != 0) return rc;

    /* AFE registers are a runtime projection of the common parameter DB.
     * Re-apply them after every AFE reset/re-initialization.
     */
    rc = bms_param_apply_hardware_all();
    if (rc != 0) return -20 + rc;
    return 0;
}

void bms_project_init(void)
{
    int rc;
    u8 mac_random_static[6];

    memset(&g_bms, 0, sizeof(g_bms));
    g_bms.soh = 100u;

    board_gpio_init();
    btname_init();
    btname_set_refresh_callback(bms_ble_refresh_name);

    /* user_init_normal() has already initialized the controller address. Calling
     * the same SDK address initializer again only retrieves the persisted/public
     * values into our protocol mirror; no private/non-SDK getter is required.
     */
    blc_initMacAddress(flash_sector_mac_address, g_bms.mac_public, mac_random_static);

    bms_ble_compat_apply();

    /* Load common logical parameters (defaults or CRC-validated A/B flash record)
     * before the AFE is initialized, then project only parameters supported by
     * the selected AFE adapter.
     */
    bms_param_init();
    rc = afe_start();
    g_bms.afe_last_error = (s16)rc;
    g_bms.afe_init_ok = (rc == 0) ? 1u : 0u;
    s_afe_fail_count = 0u;

    /* Software protection starts with user MOS requests enabled but does not
     * issue an ON request until the first complete AFE sample is evaluated.
     */
    bms_protect_init();

    modbus_uart_init();
    s_afe_poll_tick = clock_time();
}

void bms_project_process(void)
{
    int rc;

    modbus_uart_process();
    bms_param_process();

    if (clock_time_exceed(s_afe_poll_tick, BMS_AFE_POLL_PERIOD_US)) {
        s_afe_poll_tick = clock_time();
        rc = bms_afe_sample(&g_bms.afe);
        if (rc != 0) {
            g_bms.afe.online = 0u;
            g_bms.afe_last_error = (s16)rc;
            if (s_afe_fail_count < 0xFFu) ++s_afe_fail_count;

            if (s_afe_fail_count >= 10u) {
                rc = afe_start();
                g_bms.afe_init_ok = (rc == 0) ? 1u : 0u;
                g_bms.afe_last_error = (s16)rc;
                s_afe_fail_count = 0u;
                if (rc == 0)
                    bms_protect_force_mos_reapply();
            }
        } else {
            g_bms.afe_init_ok = 1u;
            g_bms.afe_last_error = 0;
            s_afe_fail_count = 0u;
            bms_protect_update(&g_bms.afe, g_bms.soc);
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
    if (index >= BMS_AFE_TEMP_CHANNELS) return 0u;
    t = g_bms.afe.temp_dC[index] + 400;
    if (t < 0) t = 0;
    return (u16)t;
}

static u16 legacy_fault_word(void)
{
    u16 f = 0;
    u32 af = g_bms.afe.fault_bits;

    if (af & BMS_AFE_FAULT_CELL_OV) f |= BIT(0);
    if (af & BMS_AFE_FAULT_CELL_UV) f |= BIT(1);
    if (af & BMS_AFE_FAULT_CHG_OC) f |= BIT(4);
    if (af & (BMS_AFE_FAULT_DSG_OC1 | BMS_AFE_FAULT_DSG_OC2 | BMS_AFE_FAULT_SHORT)) f |= BIT(5);
    if (af & BMS_AFE_FAULT_CHG_OT) f |= BIT(6);
    if (af & BMS_AFE_FAULT_DSG_OT) f |= BIT(7);
    if (af & BMS_AFE_FAULT_CHG_UT) f |= BIT(8);
    if (af & BMS_AFE_FAULT_DSG_UT) f |= BIT(9);
    return f;
}

u16 bms_project_read_legacy_d000(u16 offset)
{
    s32 ma;
    u8 cells = g_bms.afe.cell_count;

    if (cells > BMS_AFE_MAX_CELLS) cells = BMS_AFE_MAX_CELLS;
    if (offset < 32u)
        return (offset < cells) ? g_bms.afe.cell_mv[offset] : 0u;

    switch (offset) {
    case 32: return g_bms.afe.cell_max_mv;
    case 33: return g_bms.afe.cell_min_mv;
    case 34: {
        u8 i; for (i = 0; i < cells; ++i) if (g_bms.afe.cell_mv[i] == g_bms.afe.cell_max_mv) return (u16)(i + 1u);
        return 0;
    }
    case 35: {
        u8 i; for (i = 0; i < cells; ++i) if (g_bms.afe.cell_mv[i] == g_bms.afe.cell_min_mv) return (u16)(i + 1u);
        return 0;
    }
    case 36: return g_bms.afe.cell_delta_mv;
    case 37: return (u16)(g_bms.afe.pack_mv / 10u);
    case 38: return legacy_temp(0);
    case 39: return legacy_temp(1);
    case 40: return legacy_temp(2);
    case 41: return legacy_temp(3);
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
        return (ma < 0) ? (u16)((-ma) / 100) : 0u;
    case 51:
        ma = g_bms.afe.current_ma_valid ? g_bms.afe.current_ma : 0;
        return (ma > 0) ? (u16)(ma / 100) : 0u;
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
    return bms_param_read_legacy(offset);
}

int bms_project_write_protect(u16 offset, u16 value)
{
    return bms_param_write_legacy(offset, value);
}

int bms_project_write_protect_block(u16 offset, u16 qty, const u16 *values)
{
    return bms_param_write_legacy_block(offset, qty, values);
}

void bms_project_set_soc(u16 soc)
{
    g_bms.soc = (soc > 100u) ? 100u : soc;
}

int bms_project_command(u16 value)
{
    switch (value) {
    case 0x0010u:
        return bms_afe_clear_faults(BMS_AFE_FAULT_ALL) == 0;
    case 0x0011u:
        return bms_protect_request_mos(1u, 1u);
    case 0x0012u:
        return bms_protect_request_mos(0u, 0u);
    default:
        return 1;
    }
}
