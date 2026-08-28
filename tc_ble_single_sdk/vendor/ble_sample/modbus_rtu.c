#include "modbus_rtu.h"
#include "bms_project.h"
#include "bms_param.h"
#include "bms_protect.h"
#include "bms_board.h"
#include "btname_modbus.h"
#include <string.h>

#ifndef BMS_BUILD_SERIAL
#define BMS_BUILD_SERIAL "UNKNOWN-00000000"
#endif

/* BMS_BUILD_SERIAL is injected by bms_tools/build.mk from the selected AFE and
 * local build date, e.g. SH367309-20260828 / SH3673510-20260828 / MOCK-20260828.
 * This makes the production-identification registers self-describe the image
 * that was actually selected on the command line.
 */
static const u8 k_serial[] = BMS_BUILD_SERIAL;
#if (BMS_BOARD_PROFILE == BMS_BOARD_PROFILE_LEGACY_309)
static const u8 k_hwver[]  = "LEGACY-309";
#elif (BMS_BOARD_PROFILE == BMS_BOARD_PROFILE_HS_D011)
static const u8 k_hwver[]  = "HS-D011-V1";
#else
static const u8 k_hwver[]  = "UNKNOWN";
#endif
static const u8 k_swver[]  = "V1.1.0";

/* FC16 can carry at most 123 registers. Keep the scratch buffer out of the
 * TC32 stack; Modbus dispatch is serialized by the application main loop.
 */
static u16 s_write_words[123];

u16 modbus_crc16(const u8 *data, u32 len)
{
    u16 crc = 0xFFFFu;
    u8 i;
    while (len--) {
        crc ^= *data++;
        for (i = 0; i < 8; ++i)
            crc = (crc & 1u) ? (u16)((crc >> 1) ^ 0xA001u) : (u16)(crc >> 1);
    }
    return crc;
}

static u16 ascii_reg(const u8 *s, u16 maxlen, u16 off)
{
    u16 pos = (u16)(off * 2u);
    u8 hi = 0, lo = 0;
    if (pos < maxlen && s[pos]) hi = s[pos];
    if (pos + 1u < maxlen && s[pos + 1u]) lo = s[pos + 1u];
    return (u16)(((u16)hi << 8) | lo);
}

static u16 read_realtime(u16 off)
{
    const bms_project_state_t *s = bms_project_get_state();
    /* Internal AFE convention is positive=discharge, negative=charge.
     * Existing application protocol keeps positive=charge, negative=discharge.
     */
    s32 cur_a10 = s->afe.current_ma_valid ? -(s->afe.current_ma / 100) : 0;
    switch (off) {
    case 0: return BMS_REALTIME_REG_MAGIC;
    case 1: return BMS_REALTIME_REG_VERSION;
    case 2: return (u16)(s->afe.pack_mv / 10u);
    case 3: return (u16)((s16)cur_a10);
    case 4: return s->soc;
    case 5: return bms_project_read_legacy_d000(48);
    case 6: return bms_project_read_legacy_d000(49);
    case 7: return bms_project_read_legacy_d000(41);
    case 8: return s->afe.cell_max_mv;
    case 9: return s->afe.cell_min_mv;
    case 10: return s->afe.cell_delta_mv;
    default: return 0;
    }
}

static u16 read_protect_status(u16 off)
{
    const bms_project_state_t *project = bms_project_get_state();
    const bms_protect_status_t *protect = bms_protect_get_status();
    u16 mos_flags;

    if (!project || !protect) return 0u;

    mos_flags = (u16)((protect->user_charge_on ? BIT(0) : 0u) |
                      (protect->user_discharge_on ? BIT(1) : 0u) |
                      (protect->charge_veto ? BIT(2) : 0u) |
                      (protect->discharge_veto ? BIT(3) : 0u) |
                      (protect->effective_charge_on ? BIT(4) : 0u) |
                      (protect->effective_discharge_on ? BIT(5) : 0u));

    switch (off) {
    case 0: return BMS_PROTECT_STATUS_MAGIC;
    case 1: return BMS_PROTECT_STATUS_VERSION;
    case 2: return protect->l1_bitmap;
    case 3: return protect->l2_bitmap;
    case 4: return protect->l3_bitmap;
    case 5: return protect->active_bitmap;
    case 6: return mos_flags;
    case 7: return (u16)protect->last_mos_error;
    case 8: return (u16)project->afe.fault_bits;
    case 9: return (u16)(project->afe.fault_bits >> 16);
    case 10: return bms_param_persist_status_word();
    default: return 0u;
    }
}

static u16 read_reg(u16 reg)
{
    const bms_project_state_t *s = bms_project_get_state();

    if (reg < 3u) {
        u8 i = (u8)(reg * 2u);
        return (u16)(((u16)s->mac_public[i] << 8) | s->mac_public[i + 1u]);
    }

    if (reg >= BTNAME_REG_BASE && reg < BTNAME_REG_BASE + BTNAME_REG_COUNT)
        return ascii_reg((const u8 *)btname_get(), BTNAME_TOTAL_MAX_LEN,
                         (u16)(reg - BTNAME_REG_BASE));

    /* AFE-independent protocol discovery/capability/value windows. */
    if (reg >= BMS_PARAM_META_BASE && reg < BMS_PARAM_META_BASE + BMS_PARAM_META_COUNT)
        return bms_param_read_meta_reg(reg);
    if (reg >= BMS_PARAM_CAP_BASE && reg < BMS_PARAM_CAP_BASE + BMS_PARAM_CAP_REG_COUNT)
        return bms_param_read_cap_reg(reg);
    if ((reg >= BMS_PARAM_VALUE_BASE && reg < BMS_PARAM_VALUE_BASE + BMS_PARAM_VALUE_REG_COUNT) ||
        (reg >= BMS_PARAM_EFFECTIVE_BASE && reg < BMS_PARAM_EFFECTIVE_BASE + BMS_PARAM_VALUE_REG_COUNT))
        return bms_param_read_value_reg(reg);

    /* Existing applications continue to use the old 16-bit encoding. */
    if (reg >= BMS_PARAM_LEGACY_BASE && reg < BMS_PARAM_LEGACY_BASE + BMS_PARAM_COUNT)
        return bms_project_read_protect((u16)(reg - BMS_PARAM_LEGACY_BASE));

    if (reg >= PROD_SN_REG_BASE && reg < PROD_SN_REG_BASE + PROD_SN_REG_COUNT)
        return ascii_reg(k_serial, sizeof(k_serial), (u16)(reg - PROD_SN_REG_BASE));
    if (reg >= PROD_HW_VER_REG_BASE && reg < PROD_HW_VER_REG_BASE + PROD_HW_VER_REG_COUNT)
        return ascii_reg(k_hwver, sizeof(k_hwver), (u16)(reg - PROD_HW_VER_REG_BASE));
    if (reg >= PROD_SW_VER_REG_BASE && reg < PROD_SW_VER_REG_BASE + PROD_SW_VER_REG_COUNT)
        return ascii_reg(k_swver, sizeof(k_swver), (u16)(reg - PROD_SW_VER_REG_BASE));

    if (reg >= 0xD000u && reg <= 0xD03Eu)
        return bms_project_read_legacy_d000((u16)(reg - 0xD000u));

    if (reg == 0xD115u)
        return (u16)((s->afe.online ? BIT(0) : 0u) |
                     (s->afe_init_ok ? BIT(1) : 0u) |
                     ((s->afe.bstatus1 & 0x3Fu) << 8));
    if (reg == 0xD116u)
        return (u16)(((u16)s->afe.flag2 << 8) | s->afe.flag1);
    if (reg >= BMS_REALTIME_REG_BASE && reg < BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT)
        return read_realtime((u16)(reg - BMS_REALTIME_REG_BASE));
    if (reg >= BMS_PROTECT_STATUS_REG_BASE && reg < BMS_PROTECT_STATUS_REG_BASE + BMS_PROTECT_STATUS_REG_COUNT)
        return read_protect_status((u16)(reg - BMS_PROTECT_STATUS_REG_BASE));

    return 0u;
}

static int write_one(u16 reg, u16 val)
{
    if (reg >= BTNAME_REG_BASE && reg < BTNAME_REG_BASE + BTNAME_REG_COUNT)
        return btname_modbus_on_write_holding(reg, 1u, &val);

    if ((reg >= BMS_PARAM_META_BASE && reg < BMS_PARAM_META_BASE + BMS_PARAM_META_COUNT) ||
        (reg >= BMS_PARAM_CAP_BASE && reg < BMS_PARAM_CAP_BASE + BMS_PARAM_CAP_REG_COUNT) ||
        (reg >= BMS_PARAM_VALUE_BASE && reg < BMS_PARAM_VALUE_BASE + BMS_PARAM_VALUE_REG_COUNT) ||
        (reg >= BMS_PARAM_EFFECTIVE_BASE && reg < BMS_PARAM_EFFECTIVE_BASE + BMS_PARAM_VALUE_REG_COUNT) ||
        (reg >= BMS_PROTECT_STATUS_REG_BASE && reg < BMS_PROTECT_STATUS_REG_BASE + BMS_PROTECT_STATUS_REG_COUNT))
        return 0; /* metadata/capabilities/status are RO; signed32 values reject FC06 half-writes */

    if (reg >= BMS_PARAM_LEGACY_BASE && reg < BMS_PARAM_LEGACY_BASE + BMS_PARAM_COUNT)
        return bms_project_write_protect((u16)(reg - BMS_PARAM_LEGACY_BASE), val);

    if (reg == 0x1005u) {
        bms_project_set_soc(val);
        return 1;
    }
    if (reg == 0x1102u)
        return bms_project_command(val);
    return 1;
}

static void append_crc(u8 *rsp, u32 *len)
{
    u16 crc = modbus_crc16(rsp, *len);
    rsp[(*len)++] = (u8)crc;
    rsp[(*len)++] = (u8)(crc >> 8);
}

static int exception_rsp(u8 addr, u8 func, u8 ex, u8 *rsp, u32 *rsp_len)
{
    rsp[0] = addr; rsp[1] = (u8)(func | 0x80u); rsp[2] = ex;
    *rsp_len = 3u; append_crc(rsp, rsp_len); return 1;
}

static void unpack_words(const u8 *req, u16 qty)
{
    u16 i;
    for (i = 0; i < qty; ++i)
        s_write_words[i] = (u16)(((u16)req[7u + i * 2u] << 8) | req[8u + i * 2u]);
}

int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    u16 got_crc, calc_crc, reg, qty, val;
    u32 i, out;
    u8 func;

    if (rsp_len) *rsp_len = 0;
    if (!req || !rsp || !rsp_len || req_len < 4u) return 0;
    if (req[0] != MODBUS_SLAVE_ADDR) return 0;

    got_crc = (u16)(req[req_len - 2u] | ((u16)req[req_len - 1u] << 8));
    calc_crc = modbus_crc16(req, req_len - 2u);
    if (got_crc != calc_crc) return 0;

    func = req[1];
    if (func == 0x03u) {
        if (req_len != 8u) return exception_rsp(req[0], func, 0x03u, rsp, rsp_len);
        reg = (u16)(((u16)req[2] << 8) | req[3]);
        qty = (u16)(((u16)req[4] << 8) | req[5]);
        if (!qty || qty > 125u) return exception_rsp(req[0], func, 0x03u, rsp, rsp_len);
        rsp[0] = req[0]; rsp[1] = func; rsp[2] = (u8)(qty * 2u); out = 3u;
        for (i = 0; i < qty; ++i) {
            val = read_reg((u16)(reg + i));
            rsp[out++] = (u8)(val >> 8); rsp[out++] = (u8)val;
        }
        *rsp_len = out; append_crc(rsp, rsp_len); return 1;
    }

    if (func == 0x06u) {
        if (req_len != 8u) return exception_rsp(req[0], func, 0x03u, rsp, rsp_len);
        reg = (u16)(((u16)req[2] << 8) | req[3]);
        val = (u16)(((u16)req[4] << 8) | req[5]);
        if (!write_one(reg, val)) return exception_rsp(req[0], func, 0x04u, rsp, rsp_len);
        memcpy(rsp, req, 6u); *rsp_len = 6u; append_crc(rsp, rsp_len); return 1;
    }

    if (func == 0x10u) {
        u8 bytes;
        if (req_len < 9u) return exception_rsp(req[0], func, 0x03u, rsp, rsp_len);
        reg = (u16)(((u16)req[2] << 8) | req[3]);
        qty = (u16)(((u16)req[4] << 8) | req[5]);
        bytes = req[6];
        if (!qty || qty > 123u || bytes != qty * 2u || req_len != (u32)9u + bytes)
            return exception_rsp(req[0], func, 0x03u, rsp, rsp_len);

        unpack_words(req, qty);

        if (reg >= BTNAME_REG_BASE && (u32)reg + qty <= BTNAME_REG_BASE + BTNAME_REG_COUNT) {
            if (!btname_modbus_on_write_holding(reg, qty, s_write_words))
                return exception_rsp(req[0], func, 0x04u, rsp, rsp_len);
        }
        else if (reg >= BMS_PARAM_LEGACY_BASE &&
                 (u32)reg + qty <= BMS_PARAM_LEGACY_BASE + BMS_PARAM_COUNT) {
            if (!bms_project_write_protect_block((u16)(reg - BMS_PARAM_LEGACY_BASE), qty, s_write_words))
                return exception_rsp(req[0], func, 0x04u, rsp, rsp_len);
        }
        else if (reg >= BMS_PARAM_VALUE_BASE &&
                 (u32)reg + qty <= BMS_PARAM_VALUE_BASE + BMS_PARAM_VALUE_REG_COUNT) {
            if (!bms_param_write_value_block(reg, qty, s_write_words))
                return exception_rsp(req[0], func, 0x04u, rsp, rsp_len);
        }
        else {
            for (i = 0; i < qty; ++i) {
                if (!write_one((u16)(reg + i), s_write_words[i]))
                    return exception_rsp(req[0], func, 0x04u, rsp, rsp_len);
            }
        }
        memcpy(rsp, req, 6u); *rsp_len = 6u; append_crc(rsp, rsp_len); return 1;
    }

    return exception_rsp(req[0], func, 0x01u, rsp, rsp_len);
}
