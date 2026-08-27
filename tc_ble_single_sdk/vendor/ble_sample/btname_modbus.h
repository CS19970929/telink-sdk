#ifndef BTNAME_MODBUS_H_
#define BTNAME_MODBUS_H_

#include "tl_common.h"

#define BTNAME_REG_BASE         0x0100u
#define BTNAME_REG_COUNT        12u
#define BTNAME_TOTAL_MAX_LEN    25u
#define BTNAME_PREFIX           "BT_"

void btname_init(void);
const char *btname_get(void);
int btname_modbus_on_write_holding(u16 addr, u16 qty, const u16 *regs);
void btname_set_refresh_callback(void (*cb)(void));

#endif
