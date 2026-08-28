#include "bms_board.h"

#if (BMS_AFE_MODEL == BMS_AFE_MODEL_SH367309)

#include "sh367309.h"
#include <string.h>

#define SH309_RAM_START_ADDR        0x40u
#define SH309_RAM_END_ADDR          0x71u
#define SH309_RAM_LEN               (SH309_RAM_END_ADDR - SH309_RAM_START_ADDR + 1u)
#define SH309_MAX_XFER_LEN          64u
#define SH309_CONF_CADCON           BIT(3)
#define SH309_CONF_CHGMOS           BIT(4)
#define SH309_CONF_DSGMOS           BIT(5)
#define SH309_BFLAG2_READY          BIT(4)
#define SH309_CURRENT_ADC_DEN       21470u

#if (BMS_CELL_COUNT > SH367309_MAX_CELLS)
#error "SH367309 supports at most 16 cells"
#endif
#if (BMS_RSENSE_UOHM == 0)
#error "BMS_RSENSE_UOHM must be non-zero for SH367309 current conversion"
#endif

static u16 s_tr_ref_x100;
static u8 s_initialized;
static const u8 s_cell_map[BMS_CELL_COUNT] = BMS_SH309_CELL_MAP_INIT;

static u8 crc8_update(u8 crc, u8 data)
{
    u8 i;
    crc ^= data;
    for (i = 0u; i < 8u; ++i)
        crc = (crc & 0x80u) ? (u8)((crc << 1) ^ 0x07u) : (u8)(crc << 1);
    return crc;
}

static u16 be16(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

#if BMS_SH309_MOS_CONTROL_ENABLE
static void sh309_gate_gpio_init(void)
{
    gpio_set_func(BMS_SH309_AFE_CTL_PIN, AS_GPIO);
    gpio_set_input_en(BMS_SH309_AFE_CTL_PIN, 0);
    gpio_set_output_en(BMS_SH309_AFE_CTL_PIN, 1);

    gpio_set_func(BMS_SH309_MCC_C_PIN, AS_GPIO);
    gpio_set_input_en(BMS_SH309_MCC_C_PIN, 0);
    gpio_set_output_en(BMS_SH309_MCC_C_PIN, 1);

    /* Match the proven D3PRO bring-up: AFE_CTL and MCC_C start low. The power
     * path is not enabled until a complete AFE sample has been evaluated by the
     * common protection layer and the SH367309 CONF write has verified.
     */
    gpio_write(BMS_SH309_MCC_C_PIN, !BMS_SH309_MCC_C_ACTIVE_LEVEL);
    gpio_write(BMS_SH309_AFE_CTL_PIN, !BMS_SH309_GATE_ACTIVE_LEVEL);
}

static void sh309_gate_safe_off(void)
{
    gpio_write(BMS_SH309_MCC_C_PIN, !BMS_SH309_MCC_C_ACTIVE_LEVEL);
    gpio_write(BMS_SH309_AFE_CTL_PIN, !BMS_SH309_GATE_ACTIVE_LEVEL);
}

static void sh309_gate_apply(u8 charge_on, u8 discharge_on)
{
    /* Reference firmware drives MCC_C high only with CHGMOS, while AFE_CTL is
     * the common external gate/driver enable. Keep that board behavior outside
     * the register bit calculation and assert the external gate last.
     */
    gpio_write(BMS_SH309_MCC_C_PIN,
               charge_on ? BMS_SH309_MCC_C_ACTIVE_LEVEL : !BMS_SH309_MCC_C_ACTIVE_LEVEL);
    gpio_write(BMS_SH309_AFE_CTL_PIN,
               (charge_on || discharge_on) ? BMS_SH309_GATE_ACTIVE_LEVEL : !BMS_SH309_GATE_ACTIVE_LEVEL);
}
#endif

int sh367309_read(u8 reg, u8 *data, u8 len)
{
    u8 rx[SH309_MAX_XFER_LEN + 1u];
    u8 attempt;
    u8 crc;
    u8 i;

    if (!data || len == 0u || len > SH309_MAX_XFER_LEN)
        return SH367309_ERR_ARG;

    for (attempt = 0u; attempt < BMS_SH309_I2C_RETRY_COUNT; ++attempt) {
        i2c_read_series(((u16)reg << 8) | len, 2, rx, (int)len + 1);

        crc = 0u;
        crc = crc8_update(crc, BMS_SH309_I2C_ADDR);
        crc = crc8_update(crc, reg);
        crc = crc8_update(crc, len);
        crc = crc8_update(crc, (u8)(BMS_SH309_I2C_ADDR | 0x01u));
        for (i = 0u; i < len; ++i)
            crc = crc8_update(crc, rx[i]);

        if (crc == rx[len]) {
            memcpy(data, rx, len);
            return SH367309_OK;
        }
        sleep_us(1000u);
    }

    return SH367309_ERR_CRC;
}

int sh367309_write(u8 reg, const u8 *data, u8 len)
{
    u8 tx[SH309_MAX_XFER_LEN + 1u];
    u8 crc;
    u8 i;

    if (!data || len == 0u || len > SH309_MAX_XFER_LEN)
        return SH367309_ERR_ARG;

    crc = 0u;
    crc = crc8_update(crc, BMS_SH309_I2C_ADDR);
    crc = crc8_update(crc, reg);
    for (i = 0u; i < len; ++i) {
        tx[i] = data[i];
        crc = crc8_update(crc, data[i]);
    }
    tx[len] = crc;

    i2c_write_series(reg, 1, tx, (int)len + 1);
    return (reg_i2c_status & FLD_I2C_NAK) ? SH367309_ERR_IO : SH367309_OK;
}

static int write_byte_verified(u8 reg, u8 value)
{
    u8 attempt;
    u8 verify;

    for (attempt = 0u; attempt < BMS_SH309_I2C_RETRY_COUNT; ++attempt) {
        if (sh367309_write(reg, &value, 1u) == SH367309_OK) {
            sleep_us(1000u);
            if (sh367309_read(reg, &verify, 1u) == SH367309_OK && verify == value)
                return SH367309_OK;
        }
        sleep_us(1000u);
    }
    return SH367309_ERR_VERIFY;
}

static int wait_ready(void)
{
    u32 start = clock_time();
    u8 flag2;

    while (!clock_time_exceed(start, BMS_SH309_READY_TIMEOUT_MS * 1000u)) {
        if (sh367309_read(SH367309_REG_BFLAG2, &flag2, 1u) == SH367309_OK &&
            (flag2 & SH309_BFLAG2_READY))
            return SH367309_OK;
        sleep_us(20000u);
    }
    return SH367309_ERR_NOT_READY;
}

static int optional_reset(void)
{
#if BMS_SH309_RESET_ON_INIT
    u8 reset = 0xC0u;
    if (sh367309_write(SH367309_REG_SOFT_RESET, &reset, 1u) != SH367309_OK)
        return SH367309_ERR_IO;
    sleep_us(5000u);
    return wait_ready();
#else
    return SH367309_OK;
#endif
}

static u32 ntc_ohm_from_raw(u16 raw)
{
    u32 r_x100;

    if (raw >= 32769u || s_tr_ref_x100 == 0u)
        return 0xFFFFFFFFu;
    r_x100 = ((u32)s_tr_ref_x100 * raw) / (32769u - raw);
    return r_x100 * 10u; /* legacy table unit kOhm*100 -> Ohm */
}

static s16 ntc3435_to_dC(u32 ohm)
{
    static const u32 rtab[] = {
        116110u, 89350u, 69430u, 54420u, 43000u, 34220u, 27510u,
        22140u, 18010u, 14700u, 12090u, 10000u, 8310u, 6940u,
        5830u, 4920u, 4160u, 3550u, 3030u, 2600u, 2240u,
        1930u, 1670u, 1460u, 1270u, 1110u, 980u, 860u
    };
    u8 i;

    if (ohm == 0xFFFFFFFFu) return 0;
    if (ohm >= rtab[0]) return -300;
    if (ohm <= rtab[27]) return 1050;

    for (i = 0u; i < 27u; ++i) {
        if (ohm <= rtab[i] && ohm >= rtab[i + 1u]) {
            u32 span = rtab[i] - rtab[i + 1u];
            u32 pos = rtab[i] - ohm;
            return (s16)(-300 + (s16)i * 50 + (s16)((pos * 50u) / span));
        }
    }
    return 0;
}

/* Reference formula:
 *   I_mA = raw * 200 * (1000000 / Rsense_uOhm) / 21470
 * Rewritten to avoid 64-bit runtime helpers on TC32.  The quotient/remainder
 * decomposition keeps every intermediate below 32 bits for the supported
 * shunt range while preserving fractional precision.
 */
static u32 current_magnitude_ma(u16 magnitude)
{
    const u32 scale = 200000000u / BMS_RSENSE_UOHM;
    const u32 d = SH309_CURRENT_ADC_DEN;
    u32 xq = magnitude / d;
    u32 xr = magnitude % d;
    u32 sq = scale / d;
    u32 sr = scale % d;

    return xq * scale + xr * sq + (xr * sr + d / 2u) / d;
}

int sh367309_init(void)
{
    u8 tr;
    u8 probe[SH309_RAM_LEN];
    int rc;

    s_initialized = 0u;
    s_tr_ref_x100 = 0u;

#if BMS_SH309_MOS_CONTROL_ENABLE
    sh309_gate_gpio_init();
#endif

    i2c_gpio_set(BMS_SH309_I2C_GROUP);
    i2c_master_init(BMS_SH309_I2C_ADDR,
                    (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4u * BMS_SH309_I2C_HZ)));
    sleep_us(BMS_SH309_BOOT_SETTLE_MS * 1000u);

    rc = optional_reset();
    if (rc != SH367309_OK) return rc;

    rc = sh367309_read(SH367309_REG_TR, &tr, 1u);
    if (rc != SH367309_OK) return rc;
    s_tr_ref_x100 = (u16)(680u + 5u * (tr & 0x7Fu));

    /* A complete CRC-protected RAM frame is a stronger online probe than a
     * single status byte and exercises the exact transaction used at runtime.
     */
    rc = sh367309_read(SH309_RAM_START_ADDR, probe, SH309_RAM_LEN);
    if (rc != SH367309_OK) return rc;

    s_initialized = 1u;
    return SH367309_OK;
}

int sh367309_sample(sh367309_sample_t *s)
{
    u8 ram[SH309_RAM_LEN];
    u8 i;
    u16 code;
    u16 magnitude;
    u32 ma;
    int rc;

    if (!s) return SH367309_ERR_ARG;
    if (!s_initialized) return SH367309_ERR_NOT_READY;

    rc = sh367309_read(SH309_RAM_START_ADDR, ram, SH309_RAM_LEN);
    if (rc != SH367309_OK) return rc;

    memset(s, 0, sizeof(*s));
    s->conf = ram[SH367309_REG_CONF - SH309_RAM_START_ADDR];
    s->bstatus1 = ram[SH367309_REG_BSTATUS1 - SH309_RAM_START_ADDR];
    s->bstatus2 = ram[SH367309_REG_BSTATUS2 - SH309_RAM_START_ADDR];
    s->bstatus3 = ram[SH367309_REG_BSTATUS3 - SH309_RAM_START_ADDR];
    s->bflag1 = ram[SH367309_REG_BFLAG1 - SH309_RAM_START_ADDR];
    s->bflag2 = ram[SH367309_REG_BFLAG2 - SH309_RAM_START_ADDR];

    s->cell_min_mv = 0xFFFFu;
    for (i = 0u; i < BMS_CELL_COUNT; ++i) {
        u8 physical = s_cell_map[i];
        u8 off;
        u16 mv;
        if (physical >= SH367309_MAX_CELLS) return SH367309_ERR_ARG;
        off = (u8)(SH367309_REG_CELL1 - SH309_RAM_START_ADDR + physical * 2u);
        code = be16(&ram[off]);
        mv = (u16)(((u32)code * 5u) >> 5);
        s->cell_mv[i] = mv;
        s->pack_mv += mv;
        if (mv < s->cell_min_mv) s->cell_min_mv = mv;
        if (mv > s->cell_max_mv) s->cell_max_mv = mv;
    }
    s->cell_delta_mv = (u16)(s->cell_max_mv - s->cell_min_mv);

    for (i = 0u; i < SH367309_TEMP_CHANNELS; ++i) {
        u8 off = (u8)(SH367309_REG_TEMP1 - SH309_RAM_START_ADDR + i * 2u);
        s->temp_raw[i] = be16(&ram[off]);
        s->temp_ohm[i] = ntc_ohm_from_raw(s->temp_raw[i]);
        s->temp_dC[i] = ntc3435_to_dC(s->temp_ohm[i]);
    }

    /* The proven legacy data path uses CADC at 0x6E for current, not CUR at
     * 0x4C. Bit15=1 means discharge; common BMS convention is +discharge and
     * -charge, so the sign is normalized here.
     */
    code = be16(&ram[SH367309_REG_CADC - SH309_RAM_START_ADDR]);
    s->current_raw = (s16)code;
    magnitude = (code & 0x8000u) ? (u16)(0u - code) : code;
    ma = current_magnitude_ma(magnitude);
    if (ma > 0x7FFFFFFFu) ma = 0x7FFFFFFFu;
    s->current_ma = (code & 0x8000u) ? (s32)ma : -(s32)ma;
    if (magnitude == 0u) s->current_ma = 0;
    s->current_ma_valid = 1u;
    s->online = 1u;
    return SH367309_OK;
}

int sh367309_set_mos(u8 charge_on, u8 discharge_on)
{
#if BMS_SH309_MOS_CONTROL_ENABLE
    u8 conf;
    int rc;

    if (!s_initialized) {
        sh309_gate_safe_off();
        return SH367309_ERR_NOT_READY;
    }

    rc = sh367309_read(SH367309_REG_CONF, &conf, 1u);
    if (rc != SH367309_OK) {
        sh309_gate_safe_off();
        return rc;
    }

    conf |= SH309_CONF_CADCON;
    if (charge_on) conf |= SH309_CONF_CHGMOS; else conf &= (u8)~SH309_CONF_CHGMOS;
    if (discharge_on) conf |= SH309_CONF_DSGMOS; else conf &= (u8)~SH309_CONF_DSGMOS;

    rc = write_byte_verified(SH367309_REG_CONF, conf);
    if (rc != SH367309_OK) {
        sh309_gate_safe_off();
        return rc;
    }

    sh309_gate_apply(charge_on ? 1u : 0u, discharge_on ? 1u : 0u);
    return SH367309_OK;
#else
    (void)charge_on;
    (void)discharge_on;
    return SH367309_ERR_UNSUPPORTED;
#endif
}

int sh367309_clear_faults(void)
{
    /* BSTATUS is AFE-owned state and BFLAG read semantics are chip-specific.
     * Do not invent a write-to-clear operation. Add it only from verified SH309
     * documentation/bench behavior.
     */
    return SH367309_ERR_UNSUPPORTED;
}

#else

typedef int sh367309_translation_unit_not_selected_t;

#endif /* BMS_AFE_MODEL_SH367309 */
