#ifndef SH367309_H_
#define SH367309_H_

#include "tl_common.h"
#include "bms_board.h"

#define SH367309_MAX_CELLS          16u
#define SH367309_TEMP_CHANNELS      3u

#define SH367309_OK                  0
#define SH367309_ERR_ARG            (-1)
#define SH367309_ERR_IO             (-2)
#define SH367309_ERR_CRC            (-3)
#define SH367309_ERR_NOT_READY      (-4)
#define SH367309_ERR_VERIFY         (-5)
#define SH367309_ERR_UNSUPPORTED    (-6)

enum {
    SH367309_REG_TR         = 0x19,
    SH367309_REG_CONF       = 0x40,
    SH367309_REG_BALANCEH   = 0x41,
    SH367309_REG_BALANCEL   = 0x42,
    SH367309_REG_BSTATUS1   = 0x43,
    SH367309_REG_BSTATUS2   = 0x44,
    SH367309_REG_BSTATUS3   = 0x45,
    SH367309_REG_TEMP1      = 0x46,
    SH367309_REG_TEMP2      = 0x48,
    SH367309_REG_TEMP3      = 0x4A,
    SH367309_REG_CUR        = 0x4C,
    SH367309_REG_CELL1      = 0x4E,
    SH367309_REG_CADC       = 0x6E,
    SH367309_REG_BFLAG1     = 0x70,
    SH367309_REG_BFLAG2     = 0x71,
    SH367309_REG_RSTSTAT    = 0x72,
    SH367309_REG_SOFT_RESET = 0xEA
};

typedef struct {
    u16 cell_mv[SH367309_MAX_CELLS];
    u16 cell_min_mv;
    u16 cell_max_mv;
    u16 cell_delta_mv;
    u32 pack_mv;

    s16 current_raw;
    s32 current_ma;
    u8 current_ma_valid;

    u16 temp_raw[SH367309_TEMP_CHANNELS];
    u32 temp_ohm[SH367309_TEMP_CHANNELS];
    s16 temp_dC[SH367309_TEMP_CHANNELS];

    u8 conf;
    u8 bstatus1;
    u8 bstatus2;
    u8 bstatus3;
    u8 bflag1;
    u8 bflag2;
    u8 online;
} sh367309_sample_t;

int sh367309_init(void);
int sh367309_read(u8 reg, u8 *data, u8 len);
int sh367309_write(u8 reg, const u8 *data, u8 len);
int sh367309_sample(sh367309_sample_t *sample);
int sh367309_set_mos(u8 charge_on, u8 discharge_on);
int sh367309_clear_faults(void);

#endif /* SH367309_H_ */
