#include "modbus_uart.h"
#include "modbus_rtu.h"
#include "bms_board.h"
#include "app_config.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include <string.h>

#if BMS_SERIAL_ENABLE

static volatile u8 s_rx_ready;
static volatile u8 s_tx_dma_done;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;
static u8 s_rsp[512];
static u32 s_last_activity_tick;

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

static void serial_note_activity(void)
{
    s_last_activity_tick = clock_time();
}

static void serial_keep_awake(void)
{
#if BMS_SERIAL_KEEP_AWAKE && BLE_APP_PM_ENABLE
    /* The proven legacy project keeps UART active while the bus mux is in its
     * UART state. This project has no one-wire mode, so serial is permanently
     * in that state: do not let BLE Suspend gate the UART/DMA clock.
     *
     * main_loop() calls blt_pm_proc() before bms_project_process(); enforcing
     * the veto here makes SUSPEND_DISABLE the policy seen by the next BLE loop.
     */
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif
}

static void rx_rearm(void)
{
    s_rx_pkt.dma_len = 0u;
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

static void serial_hw_start(void)
{
    s_rx_ready = 0u;
    s_tx_dma_done = 0u;
    memset(&s_rx_pkt, 0, sizeof(s_rx_pkt));
    memset(&s_tx_pkt, 0, sizeof(s_tx_pkt));

    /* Keep the initialization sequence aligned with the known-good legacy
     * project: prepare RX DMA first, then mux/reset/init UART, then enable DMA
     * interrupts. UART reset does not reset DMA0 configuration.
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
    irq_enable();
}

void modbus_uart_init(void)
{
#if BMS_SERIAL_DE_ENABLE
    gpio_set_func(BMS_SERIAL_DE_PIN, AS_GPIO);
    gpio_set_input_en(BMS_SERIAL_DE_PIN, 0);
    gpio_set_output_en(BMS_SERIAL_DE_PIN, 1);
    serial_rx_mode();
#endif

    serial_hw_start();
    serial_note_activity();
    serial_keep_awake();
}

void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();

    if (irqsrc & FLD_DMA_CHN_UART_RX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
        serial_note_activity();

        /* RX DMA timeout/complete marks one RTU frame. Keep IRQ work minimal;
         * parsing and response generation stay in the main loop, matching the
         * proven legacy implementation.
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

void modbus_uart_send(const u8 *data, u32 len)
{
    if (!data || !len) return;
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
    /* uart_send_dma() clears TX_DONE before starting DMA, as recommended by
     * the Telink driver and used by the known-good legacy project.
     */
    uart_send_dma((u8 *)&s_tx_pkt);
}

void modbus_uart_process(void)
{
    /* blt_pm_proc() runs earlier in the same main-loop iteration and may restore
     * the sample BLE suspend mask. Serial-only mode owns the final veto.
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

        /* The reference implementation always rearms RX immediately after a
         * frame is consumed. Direct UART can do that before transmitting the
         * response. For DE-controlled half-duplex RS485, wait until the final
         * stop bit has left the UART before returning to RX mode.
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

    /* Error recovery is event-driven only. Do not periodically rearm a healthy
     * RX DMA channel: doing so can cut a request that starts on the recovery
     * boundary. This is intentionally simpler than the old 3 s UART/GPIO PM
     * state machine.
     */
    if (!s_rx_ready && uart_is_parity_error()) {
        uart_clear_parity_error();
        rx_rearm();
    }
}

void modbus_uart_get_pm_diag(modbus_uart_pm_diag_t *diag)
{
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
    diag->active = 1u;
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
