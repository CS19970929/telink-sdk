#include "bms_afe.h"
#include "bms_param.h"
#include "bms_board.h"
#include "sh367309.h"
#include "sh3673510.h"
#include <string.h>

#if (BMS_AFE_MODEL == BMS_AFE_MODEL_SIMULATED)

static u16 s_sim_step;

void bms_afe_get_info(bms_afe_info_t *info)
{
    if (!info) return;
    info->afe_type = BMS_AFE_TYPE_SIMULATED;
    info->cell_count = BMS_CELL_COUNT;
    info->temp_mask = (u8)(BIT(0) | BIT(1));
    info->feature_bits = 0u;
}

int bms_afe_init(void)
{
    s_sim_step = 0u;
    return 0;
}

int bms_afe_sample(bms_afe_sample_t *sample)
{
    u16 base_mv;
    u16 phase;
    u8 i;

    if (!sample) return -1;
    memset(sample, 0, sizeof(*sample));

    phase = (u16)((s_sim_step / 20u) % 5u);
    base_mv = (u16)(3300u + phase);
    sample->cell_count = BMS_CELL_COUNT;
    sample->cell_min_mv = 0xFFFFu;
    for (i = 0u; i < BMS_CELL_COUNT && i < BMS_AFE_MAX_CELLS; ++i) {
        u16 mv = (u16)(base_mv + ((u16)i * 3u + (i >> 1)) % 9u);
        sample->cell_mv[i] = mv;
        sample->pack_mv += mv;
        if (mv < sample->cell_min_mv) sample->cell_min_mv = mv;
        if (mv > sample->cell_max_mv) sample->cell_max_mv = mv;
    }
    sample->cell_delta_mv = (u16)(sample->cell_max_mv - sample->cell_min_mv);

    switch ((s_sim_step / 50u) & 0x03u) {
    case 0u: sample->current_ma = 0; break;
    case 1u: sample->current_ma = 650; break;
    case 2u: sample->current_ma = 0; break;
    default: sample->current_ma = -450; break;
    }
    sample->current_ma_valid = 1u;
    sample->temp_dC[0] = 250;
    sample->temp_dC[1] = 253;
    sample->ts_ohm[0] = 10000u;
    sample->ts_ohm[1] = 9900u;
    sample->fault_bits = 0u;
    sample->online = 1u;
    ++s_sim_step;
    return 0;
}

int bms_afe_set_mos(u8 charge_on, u8 discharge_on)
{
    (void)charge_on;
    (void)discharge_on;
    return BMS_AFE_ERR_UNSUPPORTED;
}

int bms_afe_clear_faults(u32 fault_mask)
{
    (void)fault_mask;
    return BMS_AFE_ERR_UNSUPPORTED;
}

int bms_afe_get_param_limits(u16 param_id, s32 *min_value, s32 *max_value, s32 *step)
{
    (void)param_id; (void)min_value; (void)max_value; (void)step;
    return 0;
}

int bms_afe_apply_param(u16 param_id, s32 value)
{
    (void)param_id; (void)value;
    return BMS_AFE_APPLY_NOT_MAPPED;
}

#elif (BMS_AFE_MODEL == BMS_AFE_MODEL_SH367309)

static u32 normalize_309_faults(const sh367309_sample_t *raw)
{
    u32 f = 0u;
    if (!raw) return 0u;

    if (raw->bstatus1 & BIT(0)) f |= BMS_AFE_FAULT_CELL_OV;
    if (raw->bstatus1 & BIT(1)) f |= BMS_AFE_FAULT_CELL_UV;
    if (raw->bstatus1 & BIT(2)) f |= BMS_AFE_FAULT_DSG_OC1;
    if (raw->bstatus1 & BIT(3)) f |= BMS_AFE_FAULT_DSG_OC2;
    if (raw->bstatus1 & BIT(4)) f |= BMS_AFE_FAULT_CHG_OC;
    if (raw->bstatus1 & BIT(5)) f |= BMS_AFE_FAULT_SHORT;
    if (raw->bstatus1 & BIT(6)) f |= BMS_AFE_FAULT_PF;
    if (raw->bstatus1 & BIT(7)) f |= BMS_AFE_FAULT_WDT;

    if (raw->bstatus2 & BIT(0)) f |= BMS_AFE_FAULT_CHG_UT;
    if (raw->bstatus2 & BIT(1)) f |= BMS_AFE_FAULT_CHG_OT;
    if (raw->bstatus2 & BIT(2)) f |= BMS_AFE_FAULT_DSG_UT;
    if (raw->bstatus2 & BIT(3)) f |= BMS_AFE_FAULT_DSG_OT;
    return f;
}

void bms_afe_get_info(bms_afe_info_t *info)
{
    if (!info) return;
    info->afe_type = BMS_AFE_TYPE_SH367309;
    info->cell_count = BMS_CELL_COUNT;
    info->temp_mask = (u8)((BMS_AFE_TS1_ENABLE ? BIT(0) : 0u) |
                           (BMS_AFE_TS2_ENABLE ? BIT(1) : 0u) |
                           (BMS_AFE_TS3_ENABLE ? BIT(2) : 0u));
    info->feature_bits = BMS_AFE_FEAT_CELL_OV |
                         BMS_AFE_FEAT_CELL_UV |
                         BMS_AFE_FEAT_OCD1 |
                         BMS_AFE_FEAT_OCD2 |
                         BMS_AFE_FEAT_SHORT |
                         BMS_AFE_FEAT_OCC |
                         BMS_AFE_FEAT_TEMP |
                         BMS_AFE_FEAT_BALANCE |
                         BMS_AFE_FEAT_RAW_DEBUG;
#if BMS_SH309_MOS_CONTROL_ENABLE
    info->feature_bits |= BMS_AFE_FEAT_MOS_CONTROL;
#endif
}

int bms_afe_init(void)
{
    return sh367309_init();
}

int bms_afe_sample(bms_afe_sample_t *sample)
{
    sh367309_sample_t raw;
    u8 i;
    int rc;

    if (!sample) return -1;
    rc = sh367309_sample(&raw);
    if (rc != 0) return rc;

    memset(sample, 0, sizeof(*sample));
    sample->cell_count = BMS_CELL_COUNT;
    for (i = 0u; i < BMS_CELL_COUNT && i < BMS_AFE_MAX_CELLS; ++i)
        sample->cell_mv[i] = raw.cell_mv[i];
    sample->cell_min_mv = raw.cell_min_mv;
    sample->cell_max_mv = raw.cell_max_mv;
    sample->cell_delta_mv = raw.cell_delta_mv;
    sample->pack_mv = raw.pack_mv;
    sample->current_raw = raw.current_raw;
    sample->current_ma = raw.current_ma;
    sample->current_ma_valid = raw.current_ma_valid;

    for (i = 0u; i < SH367309_TEMP_CHANNELS && i < BMS_AFE_TEMP_CHANNELS; ++i) {
        sample->ts_raw[i] = raw.temp_raw[i];
        sample->ts_ohm[i] = raw.temp_ohm[i];
        sample->temp_dC[i] = raw.temp_dC[i];
    }

    sample->fault_bits = normalize_309_faults(&raw);
    sample->flag1 = raw.bflag1;
    sample->flag2 = raw.bflag2;
    sample->flag3 = raw.bstatus3;
    sample->bstatus1 = raw.bstatus1;
    sample->bstatus2 = raw.bstatus2;
    sample->online = raw.online;
    return 0;
}

int bms_afe_set_mos(u8 charge_on, u8 discharge_on)
{
    return sh367309_set_mos(charge_on, discharge_on);
}

int bms_afe_clear_faults(u32 fault_mask)
{
    (void)fault_mask;
    return sh367309_clear_faults();
}

int bms_afe_get_param_limits(u16 param_id, s32 *min_value, s32 *max_value, s32 *step)
{
    (void)param_id; (void)min_value; (void)max_value; (void)step;
    /* Real-data phase intentionally leaves SH367309 MTP untouched. Hardware
     * projection is added only after bench-verified telemetry/current scaling.
     */
    return 0;
}

int bms_afe_apply_param(u16 param_id, s32 value)
{
    (void)param_id; (void)value;
    return BMS_AFE_APPLY_NOT_MAPPED;
}

#elif (BMS_AFE_MODEL == BMS_AFE_MODEL_SH3673510)

void bms_afe_get_info(bms_afe_info_t *info)
{
    if (!info) return;
    info->afe_type = BMS_AFE_TYPE_SH3673510;
    info->cell_count = BMS_CELL_COUNT;
    info->temp_mask = (u8)((BMS_AFE_TS1_ENABLE ? BIT(0) : 0u) |
                           (BMS_AFE_TS2_ENABLE ? BIT(1) : 0u) |
                           (BMS_AFE_TS3_ENABLE ? BIT(2) : 0u) |
                           (BMS_AFE_TS4_ENABLE ? BIT(3) : 0u));
    info->feature_bits = BMS_AFE_FEAT_CELL_OV |
                         BMS_AFE_FEAT_CELL_UV |
                         BMS_AFE_FEAT_OCD1 |
                         BMS_AFE_FEAT_OCD2 |
                         BMS_AFE_FEAT_SHORT |
                         BMS_AFE_FEAT_OCC |
                         BMS_AFE_FEAT_TEMP |
                         BMS_AFE_FEAT_BALANCE |
                         BMS_AFE_FEAT_MOS_CONTROL |
                         BMS_AFE_FEAT_RAW_DEBUG;
}

int bms_afe_init(void)
{
    return sh3673510_init();
}

static u32 normalize_3510_faults(const sh3673510_sample_t *raw)
{
    u32 f = 0u;
    if (!raw) return 0u;

    if (raw->flag1 & BIT(0)) f |= BMS_AFE_FAULT_CELL_OV;
    if (raw->flag1 & BIT(1)) f |= BMS_AFE_FAULT_CELL_UV;
    if (raw->flag1 & BIT(2)) f |= BMS_AFE_FAULT_DSG_OC1;
    if (raw->flag1 & BIT(3)) f |= BMS_AFE_FAULT_DSG_OC2;
    if (raw->flag1 & BIT(4)) f |= BMS_AFE_FAULT_SHORT;
    if (raw->flag1 & BIT(5)) f |= BMS_AFE_FAULT_CHG_OC;
    if (raw->flag1 & BIT(7)) f |= BMS_AFE_FAULT_RESET;
    if (raw->flag2 & BIT(4)) f |= BMS_AFE_FAULT_CHG_UT;
    if (raw->flag2 & BIT(5)) f |= BMS_AFE_FAULT_CHG_OT;
    if (raw->flag2 & BIT(6)) f |= BMS_AFE_FAULT_DSG_UT;
    if (raw->flag2 & BIT(7)) f |= BMS_AFE_FAULT_DSG_OT;
    if (raw->flag2 & BIT(2)) f |= BMS_AFE_FAULT_WDT;
    return f;
}

int bms_afe_sample(bms_afe_sample_t *sample)
{
    sh3673510_sample_t raw;
    u8 i;
    int rc;

    if (!sample) return -1;
    rc = sh3673510_sample(&raw);
    if (rc != 0) return rc;

    memset(sample, 0, sizeof(*sample));
    sample->cell_count = BMS_CELL_COUNT;
    for (i = 0u; i < BMS_CELL_COUNT && i < BMS_AFE_MAX_CELLS; ++i)
        sample->cell_mv[i] = raw.cell_mv[i];
    sample->cell_min_mv = raw.cell_min_mv;
    sample->cell_max_mv = raw.cell_max_mv;
    sample->cell_delta_mv = raw.cell_delta_mv;
    sample->pack_mv = raw.pack_mv;
    sample->current_raw = raw.current_raw;
    sample->current_ma = raw.current_ma;
    sample->current_ma_valid = raw.current_ma_valid;

    for (i = 0u; i < BMS_AFE_TEMP_CHANNELS; ++i) {
        sample->ts_raw[i] = raw.ts_raw[i];
        sample->ts_ohm[i] = raw.ts_ohm[i];
        sample->temp_dC[i] = raw.temp_dC[i];
    }

    sample->vtop_mv = raw.vtop_mv;
    sample->vchgr_mv = raw.vchgr_mv;
    sample->fault_bits = normalize_3510_faults(&raw);
    sample->flag1 = raw.flag1;
    sample->flag2 = raw.flag2;
    sample->flag3 = raw.flag3;
    sample->bstatus1 = raw.bstatus1;
    sample->bstatus2 = raw.bstatus2;
    sample->online = raw.online;
    return 0;
}

int bms_afe_set_mos(u8 charge_on, u8 discharge_on)
{
    return sh3673510_set_mos(charge_on, discharge_on);
}

int bms_afe_clear_faults(u32 fault_mask)
{
    u8 flag1 = 0u;
    u8 flag2 = 0u;

    if (fault_mask & BMS_AFE_FAULT_CELL_OV) flag1 |= BIT(0);
    if (fault_mask & BMS_AFE_FAULT_CELL_UV) flag1 |= BIT(1);
    if (fault_mask & BMS_AFE_FAULT_DSG_OC1) flag1 |= BIT(2);
    if (fault_mask & BMS_AFE_FAULT_DSG_OC2) flag1 |= BIT(3);
    if (fault_mask & BMS_AFE_FAULT_SHORT) flag1 |= BIT(4);
    if (fault_mask & BMS_AFE_FAULT_CHG_OC) flag1 |= BIT(5);
    if (fault_mask & BMS_AFE_FAULT_RESET) flag1 |= BIT(7);
    if (fault_mask & BMS_AFE_FAULT_WDT) flag2 |= BIT(2);
    if (fault_mask & BMS_AFE_FAULT_CHG_UT) flag2 |= BIT(4);
    if (fault_mask & BMS_AFE_FAULT_CHG_OT) flag2 |= BIT(5);
    if (fault_mask & BMS_AFE_FAULT_DSG_UT) flag2 |= BIT(6);
    if (fault_mask & BMS_AFE_FAULT_DSG_OT) flag2 |= BIT(7);
    return sh3673510_clear_flags(flag1, flag2);
}

int bms_afe_get_param_limits(u16 param_id, s32 *min_value, s32 *max_value, s32 *step)
{
    if (!min_value || !max_value || !step) return 0;
    switch (param_id) {
    case BMS_PARAM_ID_CELL_OV_L3:
        *min_value = 3000; *max_value = 4500; *step = 5; return 1;
    case BMS_PARAM_ID_CELL_UV_L3:
        *min_value = 1000; *max_value = 3500; *step = 5; return 1;
    default:
        return 0;
    }
}

int bms_afe_apply_param(u16 param_id, s32 value)
{
    switch (param_id) {
    case BMS_PARAM_ID_CELL_OV_L3:
        return sh3673510_set_ov_mv((u16)value);
    case BMS_PARAM_ID_CELL_UV_L3:
        return sh3673510_set_uv_mv((u16)value);
    default:
        return BMS_AFE_APPLY_NOT_MAPPED;
    }
}

#else
#error "BMS_AFE_MODEL has no adapter implementation"
#endif
