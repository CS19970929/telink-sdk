#include "sh3673510.h"
#include <string.h>

#define SH_CMD_WRITE       0x01u
#define SH_CMD_READ        0x02u
#define SH_CMD_SOFT_RESET  0x0Bu

#define SCONF2_LTCLR       BIT(7)
#define SCONF2_PUMP_EN     BIT(4)
#define SCONF2_DSGMOS      BIT(1)
#define SCONF2_CHGMOS      BIT(0)
#define SCONF5_MOS_EN      BIT(5)
#define SCONF5_OCC_EN      BIT(4)
#define SCONF5_CADC_EN     BIT(3)
#define SCONF5_WDT_EN      BIT(2)
#define SCONF5_WDT_3P92S   0x03u

#define SH3673510_WARMUP_US     12000u
#define SH3673510_PUMP_BUILD_US 130000u

static u8 sh_crc8_update(u8 crc, u8 data)
{
    u8 i;
    crc ^= data;
    for (i = 0; i < 8; ++i) {
        crc = (crc & 0x80u) ? (u8)((crc << 1) ^ 0x07u) : (u8)(crc << 1);
    }
    return crc;
}

static u8 sh_crc8(const u8 *p, u8 len)
{
    u8 crc = 0;
    while (len--) {
        crc = sh_crc8_update(crc, *p++);
    }
    return crc;
}

static u16 be16(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

void sh3673510_spi_init(void)
{
    /* RESET from SH3673510 is an open-drain OUTPUT to the MCU; never drive it. */
    gpio_set_func(BMS_AFE_RESET_PIN, AS_GPIO);
    gpio_set_input_en(BMS_AFE_RESET_PIN, 1);
    gpio_set_output_en(BMS_AFE_RESET_PIN, 0);

    gpio_set_func(BMS_AFE_ALARM_PIN, AS_GPIO);
    gpio_set_input_en(BMS_AFE_ALARM_PIN, 1);
    gpio_set_output_en(BMS_AFE_ALARM_PIN, 0);

    spi_master_gpio_set(BMS_AFE_SPI_GROUP);
    spi_masterCSpin_select(BMS_AFE_CS_PIN);

    /* 16 MHz / ((7 + 1) * 2) = 1 MHz. SH3673510 fixes CPOL=1, CPHA=1. */
    spi_master_init(7, SPI_MODE3);
}

int sh3673510_read(u8 reg, u8 *data, u8 len)
{
    u8 cmd[3];
    u8 rx[33];
    u8 crc;
    u8 i;

    if (!data || !len || len > 32u || reg < 0x40u || (u16)reg + len - 1u > 0x99u) {
        return -1;
    }

    cmd[0] = SH_CMD_READ;
    cmd[1] = reg;
    cmd[2] = len;
    memset(rx, 0, sizeof(rx));

    /*
     * SH3673510 SDO sequence is:
     *   0xFF, READ_CMD, REG_ADDR, DATA_LEN, DATA1..DATAN, CRC8.
     *
     * The B85 spi_read() sends all three command bytes first, then enters read
     * mode and deliberately clocks/discards exactly one byte before filling
     * Data[]. That discarded byte is SH3673510's DATA_LEN echo. Therefore the
     * returned buffer starts at DATA1, not DATA_LEN. Request N+1 bytes so the
     * caller receives N data bytes plus CRC8.
     */
    spi_read(cmd, 3, rx, (int)len + 1, BMS_AFE_CS_PIN);

    crc = 0;
    crc = sh_crc8_update(crc, 0xFFu);
    crc = sh_crc8_update(crc, SH_CMD_READ);
    crc = sh_crc8_update(crc, reg);
    crc = sh_crc8_update(crc, len);
    for (i = 0; i < len; ++i) {
        data[i] = rx[i];
        crc = sh_crc8_update(crc, data[i]);
    }
    if (crc != rx[len]) {
        return -2;
    }
    return 0;
}

int sh3673510_write(u8 reg, u8 data)
{
    u8 frame[4];
    u8 dummy = 0x00u;
    u8 verify = 0;

    if (reg < 0x40u || reg > 0x59u) {
        return -1;
    }

    /*
     * Manual wire order: CMD, REG, DATA, CRC, one invalid/dummy byte.
     * The AFE drives ACK/NACK while that final dummy byte is clocked. B85's
     * generic spi_read() discards the first post-command byte, so it cannot be
     * used to capture this ACK reliably. Send the complete timing with
     * spi_write(), then verify the register by a CRC-protected readback.
     */
    frame[0] = SH_CMD_WRITE;
    frame[1] = reg;
    frame[2] = data;
    frame[3] = sh_crc8(frame, 3);
    spi_write(frame, 4, &dummy, 1, BMS_AFE_CS_PIN);

    if (sh3673510_read(reg, &verify, 1) != 0) {
        return -2;
    }
    return (verify == data) ? 0 : -3;
}

int sh3673510_soft_reset(void)
{
    u8 frame[4];
    u8 dummy = 0x00u;
    u8 probe = 0;

    frame[0] = SH_CMD_SOFT_RESET;
    frame[1] = 0xBBu;
    frame[2] = 0xCCu;
    frame[3] = sh_crc8(frame, 3);
    spi_write(frame, 4, &dummy, 1, BMS_AFE_CS_PIN);

    sleep_us(SH3673510_WARMUP_US);
    /* Reset also resets SPI/RAM. A valid CRC-protected RAM read is the useful
     * post-reset verification; do not depend on an ACK byte the generic B85
     * helper cannot expose without consuming it.
     */
    return sh3673510_read(SH3673510_REG_SCONF1, &probe, 1);
}

static int sh_set_cell_count_10s(void)
{
    u8 v;
    if (sh3673510_read(SH3673510_REG_SCONF4, &v, 1)) return -1;
    v = (u8)((v & 0xE0u) | 10u); /* CN[4:0]=01010 for SH3673510 10S. */
    return sh3673510_write(SH3673510_REG_SCONF4, v);
}

static int sh_ensure_pump_ready(u8 *sconf2)
{
    u8 v;
    if (!sconf2) return -1;
    v = *sconf2;
    if ((v & SCONF2_PUMP_EN) == 0u) {
        v &= (u8)~(SCONF2_CHGMOS | SCONF2_DSGMOS);
        v |= SCONF2_PUMP_EN;
        if (sh3673510_write(SH3673510_REG_SCONF2, v)) return -2;
        sleep_us(SH3673510_PUMP_BUILD_US);
    }
    *sconf2 = v;
    return 0;
}

int sh3673510_init(void)
{
    u8 v;
    u8 sconf6 = 0x0Fu; /* OV/UV/OCD/SC */

    sh3673510_spi_init();
    sleep_us(SH3673510_WARMUP_US);

    if (sh3673510_soft_reset()) return -1;
    if (sh_set_cell_count_10s()) return -2;

    if (sh3673510_read(SH3673510_REG_SCONF5, &v, 1)) return -3;
    v |= (SCONF5_MOS_EN | SCONF5_OCC_EN | SCONF5_CADC_EN |
          SCONF5_WDT_EN | SCONF5_WDT_3P92S);
    if (sh3673510_write(SH3673510_REG_SCONF5, v)) return -4;

#if BMS_AFE_TS1_ENABLE
    sconf6 |= BIT(4);
#endif
#if BMS_AFE_TS2_ENABLE
    sconf6 |= BIT(5);
#endif
#if BMS_AFE_TS3_ENABLE
    sconf6 |= BIT(6);
#endif
#if BMS_AFE_TS4_ENABLE
    sconf6 |= BIT(7);
#endif
    if (sh3673510_write(SH3673510_REG_SCONF6, sconf6)) return -5;

    /* OV/UV/OCD/OCC/TEMP + WDT alarm pulses. */
    if (sh3673510_write(SH3673510_REG_ALARML, 0x5Fu)) return -6;

    /* Never enable balancing implicitly at boot. */
    if (sh3673510_write(SH3673510_REG_BAL_H, 0x00u)) return -7;
    if (sh3673510_write(SH3673510_REG_BAL_M, 0x00u)) return -8;
    if (sh3673510_write(SH3673510_REG_BAL_L, 0x00u)) return -9;

    if (sh3673510_read(SH3673510_REG_SCONF2, &v, 1)) return -10;
    /* Pump must be established before CHGMOS/DSGMOS are asserted. */
    v &= (u8)~(SCONF2_CHGMOS | SCONF2_DSGMOS);
    if (sh3673510_write(SH3673510_REG_SCONF2, v)) return -11;
    v &= (u8)~SCONF2_PUMP_EN;
    if (sh_ensure_pump_ready(&v)) return -12;
    v |= (SCONF2_CHGMOS | SCONF2_DSGMOS);
    if (sh3673510_write(SH3673510_REG_SCONF2, v)) return -13;

    return 0;
}

int sh3673510_set_mos(u8 charge_on, u8 discharge_on)
{
    u8 v;
    if (sh3673510_read(SH3673510_REG_SCONF2, &v, 1)) return -1;
    if ((charge_on || discharge_on) && sh_ensure_pump_ready(&v)) return -2;
    if (charge_on) v |= SCONF2_CHGMOS; else v &= (u8)~SCONF2_CHGMOS;
    if (discharge_on) v |= SCONF2_DSGMOS; else v &= (u8)~SCONF2_DSGMOS;
    return sh3673510_write(SH3673510_REG_SCONF2, v);
}

int sh3673510_set_balance_mask(u16 mask)
{
    mask &= 0x03FFu;
    if (sh3673510_write(SH3673510_REG_BAL_H, 0)) return -1;
    if (sh3673510_write(SH3673510_REG_BAL_M, (u8)(mask >> 8))) return -2;
    return sh3673510_write(SH3673510_REG_BAL_L, (u8)mask);
}

static int sh_set_voltage_threshold(u8 regh, u8 regl, u16 mv)
{
    u16 code;
    u8 h;
    code = (u16)(mv / 5u);
    if (code > 1023u) code = 1023u;
    if (sh3673510_read(regh, &h, 1)) return -1;
    h = (u8)((h & 0xF0u) | ((code >> 8) & 0x03u));
    if (sh3673510_write(regh, h)) return -2;
    return sh3673510_write(regl, (u8)code);
}

int sh3673510_set_ov_mv(u16 mv)
{
    return sh_set_voltage_threshold(SH3673510_REG_OVH, SH3673510_REG_OVL, mv);
}

int sh3673510_set_uv_mv(u16 mv)
{
    return sh_set_voltage_threshold(SH3673510_REG_UVH, SH3673510_REG_UVL, mv);
}

static int sh_clear_one_flag_reg(u8 flag_reg, u8 clear_mask)
{
    u8 sconf2;
    u8 flags;
    if (!clear_mask) return 0;
    if (sh3673510_read(flag_reg, &flags, 1)) return -1;
    if (sh3673510_read(SH3673510_REG_SCONF2, &sconf2, 1)) return -2;
    if (sh3673510_write(SH3673510_REG_SCONF2, (u8)(sconf2 | SCONF2_LTCLR))) return -3;
    flags &= (u8)~clear_mask;
    return sh3673510_write(flag_reg, flags);
}

int sh3673510_clear_flags(u8 flag1_mask, u8 flag2_mask)
{
    if (sh_clear_one_flag_reg(SH3673510_REG_FLAG1, flag1_mask)) return -1;
    if (sh_clear_one_flag_reg(SH3673510_REG_FLAG2, flag2_mask)) return -2;
    return 0;
}

static u32 sh_ts_to_ohm(u16 code)
{
    if (code >= 32768u) return 0xFFFFFFFFu;
    return (u32)(((u32)10000u * code) / (32768u - code));
}

static s16 sh_ntc3435_to_dC(u32 ohm)
{
    static const u32 rtab[] = {
        116110u, 89350u, 69430u, 54420u, 43000u, 34220u, 27510u,
        22140u, 18010u, 14700u, 12090u, 10000u, 8310u, 6940u,
        5830u, 4920u, 4160u, 3550u, 3030u, 2600u, 2240u,
        1930u, 1670u, 1460u, 1270u, 1110u, 980u, 860u
    };
    u8 i;

    if (ohm >= rtab[0]) return -300;
    if (ohm <= rtab[27]) return 1050;

    for (i = 0; i < 27u; ++i) {
        if (ohm <= rtab[i] && ohm >= rtab[i + 1u]) {
            u32 span = rtab[i] - rtab[i + 1u];
            u32 pos = rtab[i] - ohm;
            return (s16)(-300 + (s16)i * 50 + (s16)((pos * 50u) / span));
        }
    }
    return 0;
}

int sh3673510_sample(sh3673510_sample_t *s)
{
    u8 status[15];  /* 0x58..0x66 */
    u8 cur[2];
    u8 cells[BMS_CELL_COUNT * 2u];
    u8 high[6];     /* CADC,VTOP,VCHGR */
    u8 i;
    u16 raw;
    u32 sum = 0;

    if (!s) return -1;
    memset(s, 0, sizeof(*s));

    if (sh3673510_read(SH3673510_REG_FLAG1, status, sizeof(status))) return -2;
    if (sh3673510_read(SH3673510_REG_CUR_H, cur, 2)) return -3;
    if (sh3673510_read(SH3673510_REG_CELL1_H, cells, sizeof(cells))) return -4;
    if (sh3673510_read(SH3673510_REG_CADC_H, high, sizeof(high))) return -5;

    s->flag1 = status[0];
    s->flag2 = status[1];
    s->flag3 = status[2];
    s->bstatus1 = status[3];
    s->bstatus2 = status[4];

    for (i = 0; i < 4; ++i) {
        s->ts_raw[i] = be16(&status[5u + i * 2u]);
        s->ts_ohm[i] = sh_ts_to_ohm(s->ts_raw[i]);
        s->temp_dC[i] = sh_ntc3435_to_dC(s->ts_ohm[i]);
    }

    s->cell_min_mv = 0xFFFFu;
    for (i = 0; i < BMS_CELL_COUNT; ++i) {
        raw = be16(&cells[i * 2u]);
        s->cell_mv[i] = (u16)(((u32)raw * 5u) / 32u);
        sum += s->cell_mv[i];
        if (s->cell_mv[i] < s->cell_min_mv) s->cell_min_mv = s->cell_mv[i];
        if (s->cell_mv[i] > s->cell_max_mv) s->cell_max_mv = s->cell_mv[i];
    }
    s->pack_mv = sum;
    s->cell_delta_mv = (u16)(s->cell_max_mv - s->cell_min_mv);

    s->current_raw = (s16)be16(cur);
#if (BMS_RSENSE_UOHM > 0)
    s->current_ma = (s32)(((s64)100000000L * s->current_raw) /
                          ((s64)29127L * BMS_RSENSE_UOHM));
    s->current_ma_valid = 1u;
#else
    s->current_ma = 0;
    s->current_ma_valid = 0u;
#endif

    s->vtop_mv = (u16)(((u32)be16(&high[2]) * 125u) / 32u);
    s->vchgr_mv = (u16)(((u32)be16(&high[4]) * 125u) / 32u);
    s->online = 1u;
    return 0;
}
