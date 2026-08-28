#include "modbus_uart.h"
#include "modbus_rtu.h"
#include "bms_board.h"
#include "app_config.h"
#include "app.h"
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
static u32 s_pm_sleep_guard_tick;
static u32 s_pm_sleep_count;
static u32 s_pm_wake_count;
static u8 s_pm_sleep_guard_active;

/* app.c owns the baseline BLE sleep policy. The serial wrapper below replaces
 * its suspend-enter callback only so it can add PC3/RX pad wake while still
 * invoking the original behavior first.
 */
extern void task_sleep_enter(u8 e, u8 *p, int n);

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
    /* UART IRQs only update local state. LinkLayer PM APIs are intentionally not
     * called from DMA IRQ context; transitions into ACTIVE apply the PM veto in
     * normal/callback context.
     */
    s_last_activity_tick = clock_time();
#if BMS_SERIAL_PM_ENABLE
    s_pm_state = SERIAL_PM_ACTIVE;
    s_pm_sleep_guard_active = 0u;
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
    cpu_set_gpio_wakeup(BMS_SERIAL_RX_GPIO, BMS_SERIAL_WAKE_LEVEL, 0);
#endif

    uart_gpio_set(BMS_SERIAL_TX_PIN, BMS_SERIAL_RX_PIN);
    uart_reset();
    /* Both current board profiles use 115200 8N1 at 16 MHz. */
    uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE);
    uart_dma_enable(1, 1);
    uart_irq_enable(0, 0);

    s_rx_ready = 0u;
    s_tx_dma_done = 0u;
    memset(&s_rx_pkt, 0, sizeof(s_rx_pkt));
    memset(&s_tx_pkt, 0, sizeof(s_tx_pkt));
    serial_rx_mode();
    rx_rearm();

    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);
    irq_enable();
}

#if BMS_SERIAL_PM_ENABLE
static void serial_pm_enter_wake_armed(void)
{
    u8 wake_level_high = (BMS_SERIAL_WAKE_LEVEL == Level_High) ? 1u : 0u;

    if (s_pm_state != SERIAL_PM_ACTIVE) return;
    if (uart_tx_is_busy() || s_rx_ready || s_tx_dma_done) return;

    /* The caller has already observed the post-timeout quiet guard. Release
     * UART/DMA before remuxing PC3 to GPIO. The first request after this point
     * may be lost by design; it only has to wake the MCU.
     */
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    uart_reset();
    serial_rx_mode();

    /* Keep TX at normal UART idle HIGH while the peripheral is stopped. */
    gpio_set_func(BMS_SERIAL_TX_GPIO, AS_GPIO);
    gpio_set_input_en(BMS_SERIAL_TX_GPIO, 0);
    gpio_set_output_en(BMS_SERIAL_TX_GPIO, 1);
    gpio_write(BMS_SERIAL_TX_GPIO, 1);

    gpio_set_func(BMS_SERIAL_RX_GPIO, AS_GPIO);
    gpio_set_output_en(BMS_SERIAL_RX_GPIO, 0);
    gpio_set_input_en(BMS_SERIAL_RX_GPIO, 1);

    /* A sender can still start in the very small remux window. If RX is already
     * at the configured wake level, treat it as activity and restore UART now
     * rather than arming a level wake on an active line.
     */
    if ((gpio_read(BMS_SERIAL_RX_GPIO) ? 1u : 0u) == wake_level_high) {
        serial_hw_start();
        serial_note_activity();
        serial_pm_set_suspend_allowed(0u);
        return;
    }

    cpu_set_gpio_wakeup(BMS_SERIAL_RX_GPIO, BMS_SERIAL_WAKE_LEVEL, 1);
    s_pm_state = SERIAL_PM_WAKE_ARMED;
    s_pm_wake_pending = 0u;
    s_pm_sleep_guard_active = 0u;
    ++s_pm_sleep_count;

    /* Serial no longer vetoes BLE suspend. The BMS application wake deadline
     * still caps sleep at the existing AFE/protection scheduler deadline.
     */
    serial_pm_set_suspend_allowed(1u);
}

static void serial_pm_suspend_enter_cb(u8 e, u8 *p, int n)
{
    task_sleep_enter(e, p, n);

    /* Serial pad wake must also be enabled while advertising/disconnected. */
    if (s_pm_state == SERIAL_PM_WAKE_ARMED)
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
}

static void serial_pm_gpio_early_wakeup_cb(u8 e, u8 *p, int n)
{
    if (s_pm_state == SERIAL_PM_WAKE_ARMED) {
        /* Hold the MCU awake immediately. UART/DMA restoration stays in the
         * normal main-loop context. The wake frame may be truncated/discarded.
         */
        s_pm_state = SERIAL_PM_ACTIVE;
        s_pm_wake_pending = 1u;
        s_pm_sleep_guard_active = 0u;
        s_last_activity_tick = clock_time();
        ++s_pm_wake_count;
        serial_pm_set_suspend_allowed(0u);
    }

#if UI_KEYBOARD_ENABLE
    proc_keyboard(e, p, n);
#elif UI_BUTTON_ENABLE
    proc_button(e, p, n);
#else
    (void)e;
    (void)p;
    (void)n;
#endif
}
#endif /* BMS_SERIAL_PM_ENABLE */

void modbus_uart_init(void)
{
    memset(&s_rx_pkt, 0, sizeof(s_rx_pkt));
    memset(&s_tx_pkt, 0, sizeof(s_tx_pkt));
    s_rx_ready = 0u;
    s_tx_dma_done = 0u;
    s_pm_state = SERIAL_PM_ACTIVE;
    s_pm_wake_pending = 0u;
    s_pm_sleep_guard_active = 0u;
    s_pm_sleep_guard_tick = 0u;
    s_pm_sleep_count = 0u;
    s_pm_wake_count = 0u;

#if BMS_SERIAL_DE_ENABLE
    gpio_set_func(BMS_SERIAL_DE_PIN, AS_GPIO);
    gpio_set_input_en(BMS_SERIAL_DE_PIN, 0);
    gpio_set_output_en(BMS_SERIAL_DE_PIN, 1);
    serial_rx_mode();
#endif

    serial_hw_start();
    serial_note_activity();

#if BMS_SERIAL_PM_ENABLE
    /* The first serial-active window starts immediately after boot. */
    serial_pm_set_suspend_allowed(0u);
#endif

#if BMS_SERIAL_PM_ENABLE && BLE_APP_PM_ENABLE
    /* Install wrappers after app.c has registered its sample callbacks. */
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &serial_pm_suspend_enter_cb);
    bls_app_registerEventCallback(BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &serial_pm_gpio_early_wakeup_cb);
#endif
}

void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();

#if BMS_SERIAL_PM_ENABLE
    if (s_pm_state != SERIAL_PM_ACTIVE || s_pm_wake_pending) {
        if (irqsrc & FLD_DMA_CHN_UART_RX)
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
        if (irqsrc & FLD_DMA_CHN_UART_TX)
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
        return;
    }
#endif

    if (irqsrc & FLD_DMA_CHN_UART_RX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
        serial_note_activity();

        /* Handle a real UART error when it is reported. Do not use a periodic
         * blind DMA rearm: that can reset the receive DMA exactly while a host
         * starts a request, especially with common 1 s polling intervals.
         */
        if (uart_is_parity_error()) {
            uart_clear_parity_error();
            rx_rearm();
        } else if (s_rx_pkt.dma_len > 0u && s_rx_pkt.dma_len <= sizeof(s_rx_pkt.data)) {
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

void modbus_uart_send(const u8 *data, u32 len)
{
    if (!data || !len) return;
#if BMS_SERIAL_PM_ENABLE
    if (s_pm_state != SERIAL_PM_ACTIVE || s_pm_wake_pending) return;
#endif
    if (len > sizeof(s_tx_pkt.data)) len = sizeof(s_tx_pkt.data);

    while (uart_tx_is_busy()) { }
    serial_note_activity();
    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, data, len);
    s_tx_dma_done = 0u;
    serial_tx_mode();
#if BMS_SERIAL_DE_ENABLE
    sleep_us(2);
#endif
    uart_send_dma((u8 *)&s_tx_pkt);
}

void modbus_uart_process(void)
{
#if BMS_SERIAL_PM_ENABLE
    if (s_pm_wake_pending) {
        s_pm_wake_pending = 0u;
        serial_hw_start();
        serial_note_activity();
        serial_pm_set_suspend_allowed(0u);
        /* Deliberately discard any bytes from the wake frame. */
    }

    if (s_pm_state == SERIAL_PM_WAKE_ARMED)
        return;
#endif

    if (s_tx_dma_done && !uart_tx_is_busy()) {
        s_tx_dma_done = 0u;
        serial_rx_mode();
        rx_rearm();
        serial_note_activity();
    }

    if (s_rx_ready) {
        u32 req_len = s_rx_pkt.dma_len;
        u32 rsp_len = 0u;
        s_rx_ready = 0u;
        serial_note_activity();

        if (req_len > 0u && req_len <= sizeof(s_rx_pkt.data) &&
            modbus_on_frame(s_rx_pkt.data, req_len, s_rsp, &rsp_len) && rsp_len)
            modbus_uart_send(s_rsp, rsp_len);
        else
            rx_rearm();
    }

#if BMS_SERIAL_PM_ENABLE
    /* Do not tear UART down at the exact 3-second boundary. A request can begin
     * just before its RX-timeout/DMA interrupt becomes visible to software. The
     * extra quiet guard lets any in-flight Modbus frame finish and refresh the
     * activity timer before the UART-to-GPIO transition is allowed.
     */
    if (s_pm_state == SERIAL_PM_ACTIVE &&
        !s_rx_ready && !s_tx_dma_done && !uart_tx_is_busy() &&
        clock_time_exceed(s_last_activity_tick, BMS_SERIAL_IDLE_SLEEP_MS * 1000u)) {
        if (!s_pm_sleep_guard_active) {
            s_pm_sleep_guard_active = 1u;
            s_pm_sleep_guard_tick = clock_time();
        } else if (clock_time_exceed(s_pm_sleep_guard_tick,
                                     BMS_SERIAL_SLEEP_GUARD_MS * 1000u)) {
            serial_pm_enter_wake_armed();
        }
    } else {
        s_pm_sleep_guard_active = 0u;
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
    diag->last_activity_tick = s_last_activity_tick;
#else
    diag->active = 1u;
#endif
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
