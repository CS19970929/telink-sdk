#include "modbus_uart.h"
#include "modbus_rtu.h"
#include "bms_board.h"
#include "drivers.h"
#include <string.h>

#if BMS_RS485_ENABLE

static volatile u8 s_rx_ready;
static volatile u8 s_tx_dma_done;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;
static u8 s_rsp[512];
static u32 s_last_rearm_tick;

static void rs485_rx_mode(void)
{
    gpio_write(BMS_RS485_EN_PIN, 0);
}

static void rs485_tx_mode(void)
{
    gpio_write(BMS_RS485_EN_PIN, 1);
}

static void rx_rearm(void)
{
    s_rx_pkt.dma_len = 0;
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

void modbus_uart_init(void)
{
    memset(&s_rx_pkt, 0, sizeof(s_rx_pkt));
    memset(&s_tx_pkt, 0, sizeof(s_tx_pkt));

    gpio_set_func(BMS_RS485_EN_PIN, AS_GPIO);
    gpio_set_input_en(BMS_RS485_EN_PIN, 0);
    gpio_set_output_en(BMS_RS485_EN_PIN, 1);
    rs485_rx_mode();

    uart_gpio_set(BMS_RS485_TX_PIN, BMS_RS485_RX_PIN);
    uart_reset();
    uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE); /* 115200 8N1 @16 MHz */
    uart_dma_enable(1, 1);
    uart_irq_enable(0, 0);
    rx_rearm();

    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);
    irq_enable();
    s_last_rearm_tick = clock_time();
}

void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();
    if (irqsrc & FLD_DMA_CHN_UART_RX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
        if (s_rx_pkt.dma_len > 0u && s_rx_pkt.dma_len <= sizeof(s_rx_pkt.data))
            s_rx_ready = 1u;
        else
            rx_rearm();
    }
    if (irqsrc & FLD_DMA_CHN_UART_TX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
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
    rs485_tx_mode();
    sleep_us(2);
    uart_send_dma((u8 *)&s_tx_pkt);
}

void modbus_uart_process(void)
{
    if (s_tx_dma_done && !uart_tx_is_busy()) {
        s_tx_dma_done = 0u;
        rs485_rx_mode();
        rx_rearm();
        s_last_rearm_tick = clock_time();
    }

    if (s_rx_ready) {
        u32 req_len = s_rx_pkt.dma_len;
        u32 rsp_len = 0;
        s_rx_ready = 0u;

        if (req_len > 0u && req_len <= sizeof(s_rx_pkt.data) &&
            modbus_on_frame(s_rx_pkt.data, req_len, s_rsp, &rsp_len) && rsp_len)
            modbus_uart_send(s_rsp, rsp_len);
        else
            rx_rearm();
        s_last_rearm_tick = clock_time();
    }

    if (!s_rx_ready && !uart_tx_is_busy() && clock_time_exceed(s_last_rearm_tick, 1000000u)) {
        if (uart_is_parity_error()) uart_clear_parity_error();
        rs485_rx_mode();
        rx_rearm();
        s_last_rearm_tick = clock_time();
    }
}

#else

/* Transport stub: keeps the deterministic source list unchanged while a board
 * profile has no compatible physical RS485 transceiver. BLE Modbus uses the
 * same modbus_on_frame() parser and is unaffected.
 */
void modbus_uart_init(void) { }
void modbus_uart_irq_proc(void) { }
void modbus_uart_process(void) { }
void modbus_uart_send(const u8 *data, u32 len) { (void)data; (void)len; }

#endif /* BMS_RS485_ENABLE */
