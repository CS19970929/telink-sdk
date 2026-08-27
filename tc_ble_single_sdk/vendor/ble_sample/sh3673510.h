#ifndef SH3673510_H_
#define SH3673510_H_

#include "tl_common.h"
#include "hs_d011_board.h"

enum {
    SH3673510_REG_SCONF1   = 0x40,
    SH3673510_REG_SCONF2   = 0x41,
    SH3673510_REG_SCONF3   = 0x42,
    SH3673510_REG_SCONF4   = 0x43,
    SH3673510_REG_SCONF5   = 0x44,
    SH3673510_REG_SCONF6   = 0x45,
    SH3673510_REG_SCONF7   = 0x46,
    SH3673510_REG_ALARMH   = 0x47,
    SH3673510_REG_ALARML   = 0x48,
    SH3673510_REG_OVH      = 0x49,
    SH3673510_REG_OVL      = 0x4A,
    SH3673510_REG_UVH      = 0x4B,
    SH3673510_REG_UVL      = 0x4C,
    SH3673510_REG_OCD1     = 0x4D,
    SH3673510_REG_OCD2     = 0x4E,
    SH3673510_REG_SC       = 0x4F,
    SH3673510_REG_OCC      = 0x50,
    SH3673510_REG_OTC      = 0x51,
    SH3673510_REG_OTD      = 0x52,
    SH3673510_REG_UTC      = 0x53,
    SH3673510_REG_UTD      = 0x54,
    SH3673510_REG_BAL_H    = 0x55,
    SH3673510_REG_BAL_M    = 0x56,
    SH3673510_REG_BAL_L    = 0x57,
    SH3673510_REG_FLAG1    = 0x58,
    SH3673510_REG_FLAG2    = 0x59,
    SH3673510_REG_FLAG3    = 0x5A,
    SH3673510_REG_BSTATUS1 = 0x5B,
    SH3673510_REG_BSTATUS2 = 0x5C,
    SH3673510_REG_TS1_H    = 0x5D,
    SH3673510_REG_CUR_H    = 0x67,
    SH3673510_REG_CELL1_H  = 0x69,
    SH3673510_REG_CADC_H   = 0x91,
    SH3673510_REG_VTOP_H   = 0x93,
    SH3673510_REG_VCHGR_H  = 0x95,
};

typedef struct {
    u16 cell_mv[BMS_CELL_COUNT];
    u16 cell_min_mv;
    u16 cell_max_mv;
    u16 cell_delta_mv;
    u32 pack_mv;
    s16 current_raw;
    s32 current_ma;
    u8  current_ma_valid;
    u16 ts_raw[4];
    u32 ts_ohm[4];
    s16 temp_dC[4];
    u16 vtop_mv;
    u16 vchgr_mv;
    u8 flag1;
    u8 flag2;
    u8 flag3;
    u8 bstatus1;
    u8 bstatus2;
    u8 online;
} sh3673510_sample_t;

void sh3673510_spi_init(void);
int  sh3673510_soft_reset(void);
int  sh3673510_init(void);
int  sh3673510_read(u8 reg, u8 *data, u8 len);
int  sh3673510_write(u8 reg, u8 data);
int  sh3673510_sample(sh3673510_sample_t *sample);
int  sh3673510_set_mos(u8 charge_on, u8 discharge_on);
int  sh3673510_set_balance_mask(u16 mask);
int  sh3673510_set_ov_mv(u16 mv);
int  sh3673510_set_uv_mv(u16 mv);
int  sh3673510_clear_flags(u8 flag1_mask, u8 flag2_mask);

#endif /* SH3673510_H_ */
