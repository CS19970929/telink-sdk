#ifndef MODBUS_UART_H_
#define MODBUS_UART_H_

#include "tl_common.h"

#define MODBUS_UART_DATA_MAX  268u

typedef struct {
    u32 dma_len;
    u8 data[MODBUS_UART_DATA_MAX];
} mb_dma_pkt_t;

void modbus_uart_init(void);
void modbus_uart_irq_proc(void);
void modbus_uart_process(void);
void modbus_uart_send(const u8 *data, u32 len);

#endif
