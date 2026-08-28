#include "modbus_uart.h"
#include "modbus_rtu.h"
#include "bms_board.h"
#include "app_config.h"
#include "app_ui.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include <string.h>

#if BMS_SERIAL_ENABLE

#define SERIAL_PM_ACTIVE       0u
#define SERIAL_PM_WAKE_ARMED   1u

static volatile u8 s_rx_ready;
static volatile u8 s_tx_dma_done;
static volatile u8 s_pm_state;
static volatile u8 s_pm_wake_pending;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;
static u8 s_rsp[512];
static u32 s_last_activity_tick;
static u32 s_pm_guard_tick;
static u32 s_pm_sleep_count;
static u32 s_pm_wake_count;
static u8 s_pm_guard_active;

/* app.c owns the normal BLE suspend-enter policy. When serial PM is enabled we
 * replace that callback with a wrapper, invoke the original behavior first,
 * then add the UART RX pad as a wake source only while serial is sleep-armed.
 */
#if BMS_SERIAL_PM_ENABLE && BLE_APP_PM_ENABLE
extern void task_sleep_enter(u8 e, u8 *p, int n);
#endif

static void serial_rx_mode(void)
{
#if BMS_SERIAL_DE_ENABLE
    gpio_write(BMS_SERIAL_DE_PIN, 0);
#endif
}

static void serial_tx_mode(void)
{
#if BMS_SERIAL_DE_ENABLE
    gpio_write(BMS_SERIAL_DE_PIN, 1);
#endif
}

static void serial_pm_set_suspend_allowed(u8 allowed)
{
#if BLE_APP_PM_ENABLE
    if (!allowed) {
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        return;
    }

#if PM_DEEPSLEEP_RETENTION_ENABLE
    bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV |
                          SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
#else
    bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#endif
#else
    (void)allowed;
#endif
}

static void serial_note_activity(void)
{
    s_last_activity_tick = clock_time();
#if BMS_SERIAL_PM_ENABLE
    s_pm_guard_active = 0u;
#endif
}

static void serial_keep_awake(void)
{
#if BLE_APP_PM_ENABLE
#if BMS_SERIAL_PM_ENABLE
    if (s_pm_state == SERIAL_PM_ACTIVE)
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
#elif BMS_SERIAL_KEEP_AWAKE
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif
#endif
}

static void rx_rearm(void)
{
    s_rx_pkt.dma_len = 0u;
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

static void serial_hw_start(void)
{
#if BMS_SERIAL_PM_ENABLE
    /* Disable both wake mechanisms before returning PC3 to the UART mux. */
    cpu_set_gpio_wakeup(BMS_SERIAL_RX_GPIO, BMS_SERIAL_WAKE_LEVEL, 0);
    gpio_en_interrupt_risc0(BMS_SERIAL_RX_GPIO, 0);
    gpio_clr_irq_status(GPIO_IRQ_GPIO2RISC0_STATUS);
    gpio_setup_up_down_resistor(BMS_SERIAL_RX_GPIO, PM_PIN_UP_DOWN_FLOAT);
#endif

    s_rx_ready = 0u;
    s_tx_dma_done = 0u;
    memset(&s_rx_pkt, 0, sizeof(s_rx_pkt));
    memset(&s_tx_pkt, 0, sizeof(s_tx_pkt));

    /* Keep the active UART sequence aligned with the known-good legacy project:
     * prepare RX DMA first, mux pins, reset/init UART, then enable DMA + IRQ.
     * Low-power code never touches this sequence while SERIAL_PM_ACTIVE.
     */
    rx_rearm();
    uart_gpio_set(BMS_SERIAL_TX_PIN, BMS_SERIAL_RX_PIN);
    uart_reset();
    uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE); /* 115200 8N1 @ 16 MHz */
    uart_dma_enable(1, 1);
    uart_irq_enable(0, 0);

    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);
    serial_rx_mode();
    irq_enable();
}

#if BMS_SERIAL_PM_ENABLE
static void serial_pm_enter_sleep(void)
{
    if (s_pm_state != SERIAL_PM_ACTIVE) return;
    if (s_rx_ready || s_tx_dma_done || uart_tx_is_busy()) return;

    /* Leave the proven UART path only after the complete 3 s idle interval plus
     * a quiet guard. The first request after this point may be lost by design.
     */
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    uart_dma_enable(0, 0);
    uart_irq_enable(0, 0);
    serial_rx_mode();

    /* Keep TX at the standard UART idle level while the peripheral is asleep. */
    gpio_set_func(BMS_SERIAL_TX_GPIO, AS_GPIO);
    gpio_set_input_en(BMS_SERIAL_TX_GPIO, 0);
    gpio_set_output_en(BMS_SERIAL_TX_GPIO, 1);
    gpio_write(BMS_SERIAL_TX_GPIO, 1);

    /* RX idle is HIGH. A weak pull-up prevents a disconnected/floating cable
     * from repeatedly waking the MCU. Falling edges are detected by RISC0 while
     * awake and the same LOW level is also armed as a suspend/deep-sleep pad.
     */
    gpio_set_func(BMS_SERIAL_RX_GPIO, AS_GPIO);
    gpio_set_output_en(BMS_SERIAL_RX_GPIO, 0);
    gpio_set_input_en(BMS_SERIAL_RX_GPIO, 1);
    gpio_setup_up_down_resistor(BMS_SERIAL_RX_GPIO, BMS_SERIAL_RX_SLEEP_PULL);

    gpio_set_interrupt_risc0(BMS_SERIAL_RX_GPIO, POL_FALLING);
    gpio_en_interrupt_risc0(BMS_SERIAL_RX_GPIO, 1);
    cpu_set_gpio_wakeup(BMS_SERIAL_RX_GPIO, BMS_SERIAL_WAKE_LEVEL, 1);

    s_pm_state = SERIAL_PM_WAKE_ARMED;
    s_pm_wake_pending = 0u;
    s_pm_guard_active = 0u;
    ++s_pm_sleep_count;

    /* blt_pm_proc ran before bms_project_process in this loop and serial ACTIVE
     * may have vetoed its policy on the prior loop. Restore the normal BLE mask
     * immediately so the next LinkLayer iteration can actually suspend.
     */
    serial_pm_set_suspend_allowed(1u);
    bls_pm_setWakeupSource(PM_WAKEUP_PAD);
}

static void serial_pm_request_wake(void)
{
    if (s_pm_state != SERIAL_PM_WAKE_ARMED || s_pm_wake_pending) return;

    /* Do not restore UART in IRQ/callback context. Stop further wake events from
     * the remainder of the first frame and let the normal project loop restore
     * the exact known-good UART/DMA sequence. The current frame may be lost.
     */
    gpio_en_interrupt_risc0(BMS_SERIAL_RX_GPIO, 0);
    cpu_set_gpio_wakeup(BMS_SERIAL_RX_GPIO, BMS_SERIAL_WAKE_LEVEL, 0);
    s_pm_wake_pending = 1u;
}

static void serial_pm_restore_from_wake(void)
{
    if (!s_pm_wake_pending || s_pm_state != SERIAL_PM_WAKE_ARMED) return;

    s_pm_wake_pending = 0u;
    serial_hw_start();
    s_pm_state = SERIAL_PM_ACTIVE;
    ++s_pm_wake_count;
    serial_note_activity();
    serial_pm_set_suspend_allowed(0u);
}

static void serial_pm_suspend_enter_cb(u8 e, u8 *p, int n)
{
#if BLE_APP_PM_ENABLE
    task_sleep_enter(e, p, n);
    if (s_pm_state == SERIAL_PM_WAKE_ARMED)
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
#else
    (void)e; (void)p; (void)n;
#endif
}

static void serial_pm_gpio_early_wakeup_cb(u8 e, u8 *p, int n)
{
    /* PAD wake is the authoritative wake path from Suspend. RISC0 below is kept
     * as the same falling-edge detector used by the proven legacy bus mux, so
     * serial wake also works when the CPU happens to be awake for a BMS task.
     */
    serial_pm_request_wake();

#if UI_KEYBOARD_ENABLE
    proc_keyboard(e, p, n);
#elif UI_BUTTON_ENABLE
    proc_button(e, p, n);
#else
    (void)e; (void)p; (void)n;
#endif
}
#endif /* BMS_SERIAL_PM_ENABLE */

void modbus_uart_init(void)
{
#if BMS_SERIAL_DE_ENABLE
    gpio_set_func(BMS_SERIAL_DE_PIN, AS_GPIO);
    gpio_set_input_en(BMS_SERIAL_DE_PIN, 0);
    gpio_set_output_en(BMS_SERIAL_DE_PIN, 1);
    serial_rx_mode();
#endif

    s_pm_state = SERIAL_PM_ACTIVE;
    s_pm_wake_pending = 0u;
    s_pm_guard_active = 0u;
    s_pm_guard_tick = 0u;
    s_pm_sleep_count = 0u;
    s_pm_wake_count = 0u;

    serial_hw_start();
    serial_note_activity();
    serial_keep_awake();

#if BMS_SERIAL_PM_ENABLE && BLE_APP_PM_ENABLE
    /* bms_project_init() runs after user_init_normal(), so app.c has already
     * registered its callbacks. These wrappers become the final callbacks while
     * preserving task_sleep_enter and optional keyboard/button behavior.
     */
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER,
                                  &serial_pm_suspend_enter_cb);
    bls_app_registerEventCallback(BLT_EV_FLAG_GPIO_EARLY_WAKEUP,
                                  &serial_pm_gpio_early_wakeup_cb);
#endif
}

void modbus_uart_irq_proc(void)
{
#if BMS_SERIAL_PM_ENABLE
    if (s_pm_state == SERIAL_PM_WAKE_ARMED) {
        if (reg_irq_src & FLD_IRQ_GPIO_RISC0_EN) {
            reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;
            serial_pm_request_wake();
        }
        return;
    }
#endif

    {
        u8 irqsrc = dma_chn_irq_status_get();

        if (irqsrc & FLD_DMA_CHN_UART_RX) {
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
            serial_note_activity();

            /* RX DMA timeout/complete marks one RTU frame. Keep IRQ work minimal;
             * parsing and response generation stay in the main loop, matching
             * the proven legacy implementation.
             */
            if (s_rx_pkt.dma_len > 0u && s_rx_pkt.dma_len <= sizeof(s_rx_pkt.data)) {
                s_rx_ready = 1u;
            } else {
                rx_rearm();
            }
        }

        if (irqsrc & FLD_DMA_CHN_UART_TX) {
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
            serial_note_activity();
            s_tx_dma_done = 1u;
        }
    }
}

void modbus_uart_send(const u8 *data, u32 len)
{
    if (!data || !len) return;
#if BMS_SERIAL_PM_ENABLE
    if (s_pm_state != SERIAL_PM_ACTIVE || s_pm_wake_pending) return;
#endif
    if (len > sizeof(s_tx_pkt.data)) len = sizeof(s_tx_pkt.data);

    while (uart_tx_is_busy()) { }

    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, data, len);
    s_tx_dma_done = 0u;
    serial_note_activity();
    serial_tx_mode();
#if BMS_SERIAL_DE_ENABLE
    sleep_us(2);
#endif
    uart_send_dma((u8 *)&s_tx_pkt);
}

void modbus_uart_process(void)
{
#if BMS_SERIAL_PM_ENABLE
    /* GPIO/PAD wake only posts a request in IRQ/callback context. Re-enter the
     * exact stable UART path here, after app/main processing and before the next
     * BLE LinkLayer iteration. The wake frame itself is intentionally disposable.
     */
    if (s_pm_wake_pending)
        serial_pm_restore_from_wake();

    if (s_pm_state == SERIAL_PM_WAKE_ARMED)
        return;
#endif

    /* blt_pm_proc() runs earlier in the same main-loop iteration and restores
     * normal BLE suspend policy. While UART is active, serial owns the final
     * veto so UART/DMA remain identical to the already bench-proven baseline.
     */
    serial_keep_awake();

    if (s_tx_dma_done && !uart_tx_is_busy()) {
        s_tx_dma_done = 0u;
#if BMS_SERIAL_DE_ENABLE
        serial_rx_mode();
        rx_rearm();
#endif
        serial_note_activity();
    }

    if (s_rx_ready) {
        u32 req_len = s_rx_pkt.dma_len;
        u32 rsp_len = 0u;
        int ok;

        s_rx_ready = 0u;
        serial_note_activity();

        ok = (req_len > 0u && req_len <= sizeof(s_rx_pkt.data)) ?
             modbus_on_frame(s_rx_pkt.data, req_len, s_rsp, &rsp_len) : 0;

        /* Direct UART follows the proven reference: consume one frame and rearm
         * RX immediately. Half-duplex RS485 waits until the response completes
         * before returning DE low and rearming RX.
         */
#if BMS_SERIAL_DE_ENABLE
        if (ok && rsp_len) {
            modbus_uart_send(s_rsp, rsp_len);
        } else {
            rx_rearm();
        }
#else
        rx_rearm();
        if (ok && rsp_len)
            modbus_uart_send(s_rsp, rsp_len);
#endif
    }

    /* Event-driven UART error recovery only; never periodically rearm a healthy
     * DMA channel because that can cut a request in progress.
     */
    if (!s_rx_ready && uart_is_parity_error()) {
        uart_clear_parity_error();
        rx_rearm();
        serial_note_activity();
    }

#if BMS_SERIAL_PM_ENABLE
    /* Three seconds is measured from the last RX/TX activity. Keep UART fully
     * active for an additional short quiet guard so a request that starts on the
     * timeout boundary can finish and refresh last_activity before remuxing RX.
     */
    if (!s_rx_ready && !s_tx_dma_done && !uart_tx_is_busy() &&
        clock_time_exceed(s_last_activity_tick,
                          BMS_SERIAL_IDLE_SLEEP_MS * 1000u)) {
        if (!s_pm_guard_active) {
            s_pm_guard_active = 1u;
            s_pm_guard_tick = clock_time();
        } else if (clock_time_exceed(s_pm_guard_tick,
                                     BMS_SERIAL_SLEEP_GUARD_MS * 1000u)) {
            serial_pm_enter_sleep();
        }
    } else {
        s_pm_guard_active = 0u;
    }
#endif
}

void modbus_uart_get_pm_diag(modbus_uart_pm_diag_t *diag)
{
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
#if BMS_SERIAL_PM_ENABLE
    diag->active = (s_pm_state == SERIAL_PM_ACTIVE) ? 1u : 0u;
    diag->wake_armed = (s_pm_state == SERIAL_PM_WAKE_ARMED) ? 1u : 0u;
    diag->wake_pending = s_pm_wake_pending;
    diag->sleep_count = s_pm_sleep_count;
    diag->wake_count = s_pm_wake_count;
#else
    diag->active = 1u;
#endif
    diag->last_activity_tick = s_last_activity_tick;
}

#else

void modbus_uart_init(void) { }
void modbus_uart_irq_proc(void) { }
void modbus_uart_process(void) { }
void modbus_uart_send(const u8 *data, u32 len) { (void)data; (void)len; }
void modbus_uart_get_pm_diag(modbus_uart_pm_diag_t *diag)
{
    if (diag) memset(diag, 0, sizeof(*diag));
}

#endif /* BMS_SERIAL_ENABLE */
