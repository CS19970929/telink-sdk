#include "bms_protect.h"
#include "bms_param.h"
#include "bms_board.h"
#include <string.h>

#define BMS_PROTECT_LEVEL_COUNT 3u

static bms_protect_status_t s_status;
static u8 s_pending[BMS_PARAM_GROUP_COUNT][BMS_PROTECT_LEVEL_COUNT];
static u32 s_pending_tick[BMS_PARAM_GROUP_COUNT][BMS_PROTECT_LEVEL_COUNT];
static u8 s_applied_charge = 0xFFu;
static u8 s_applied_discharge = 0xFFu;
static u8 s_have_sample;
static u8 s_last_current_valid;
static s32 s_last_current_ma;

static u16 group_bit(u16 group)
{
    return (u16)BIT(group);
}

static u16 *level_bitmap(u8 level)
{
    if (level == 0u) return &s_status.l1_bitmap;
    if (level == 1u) return &s_status.l2_bitmap;
    return &s_status.l3_bitmap;
}

static void set_level_state(u16 group, u8 level, u8 on)
{
    u16 *bits = level_bitmap(level);
    u16 mask = group_bit(group);

    if (on)
        *bits |= mask;
    else
        *bits &= (u16)~mask;
}

static u8 get_level_state(u16 group, u8 level)
{
    return ((*level_bitmap(level)) & group_bit(group)) ? 1u : 0u;
}

static int get_param_requested(u16 group, u16 field, s32 *value)
{
    u16 index = (u16)(group * BMS_PARAM_FIELDS_PER_GROUP + field);
    bms_param_desc_t d;
    bms_param_value_t v;

    if (!value || !bms_param_get_desc(index, &d) || !bms_param_get_value(index, &v))
        return 0;
    if ((d.flags & BMS_PARAM_FLAG_ACTIVE) == 0u) return 0;
    *value = v.requested_value;
    return 1;
}

static int external_temp_extremes(const bms_afe_sample_t *sample, s32 *min_temp, s32 *max_temp)
{
    bms_afe_info_t info;
    u8 i;
    u8 found = 0u;
    s32 tmin = 0;
    s32 tmax = 0;

    if (!sample || !min_temp || !max_temp) return 0;
    bms_afe_get_info(&info);

    /* TS4 is the dedicated MOS sensor on boards that provide it. Battery
     * charge/discharge temperature protection therefore uses enabled TS1..TS3.
     */
    for (i = 0u; i < 3u && i < BMS_AFE_TEMP_CHANNELS; ++i) {
        if ((info.temp_mask & BIT(i)) == 0u) continue;
        if (!found) {
            tmin = sample->temp_dC[i];
            tmax = sample->temp_dC[i];
            found = 1u;
        } else {
            if (sample->temp_dC[i] < tmin) tmin = sample->temp_dC[i];
            if (sample->temp_dC[i] > tmax) tmax = sample->temp_dC[i];
        }
    }

    if (!found) return 0;
    *min_temp = tmin;
    *max_temp = tmax;
    return 1;
}

static int group_measurement(u16 group, const bms_afe_sample_t *sample, u16 soc,
                             s32 *measurement, u8 *is_high)
{
    bms_afe_info_t info;
    s32 tmin;
    s32 tmax;

    if (!sample || !measurement || !is_high) return 0;

    switch (group) {
    case BMS_PARAM_GROUP_CELL_OV:
        *measurement = sample->cell_max_mv;
        *is_high = 1u;
        return sample->cell_count ? 1 : 0;

    case BMS_PARAM_GROUP_CELL_UV:
        *measurement = sample->cell_min_mv;
        *is_high = 0u;
        return sample->cell_count ? 1 : 0;

    case BMS_PARAM_GROUP_BUS_OV:
    case BMS_PARAM_GROUP_BUS_UV:
        return 0;

    case BMS_PARAM_GROUP_CHG_OC:
        if (!sample->current_ma_valid || sample->current_ma >= 0) return 0;
        *measurement = -sample->current_ma;
        *is_high = 1u;
        return 1;

    case BMS_PARAM_GROUP_DSG_OC:
        if (!sample->current_ma_valid || sample->current_ma <= 0) return 0;
        *measurement = sample->current_ma;
        *is_high = 1u;
        return 1;

    case BMS_PARAM_GROUP_CHG_OT:
        if (!sample->current_ma_valid || sample->current_ma >= 0 ||
            !external_temp_extremes(sample, &tmin, &tmax)) return 0;
        *measurement = tmax;
        *is_high = 1u;
        return 1;

    case BMS_PARAM_GROUP_CHG_UT:
        if (!sample->current_ma_valid || sample->current_ma >= 0 ||
            !external_temp_extremes(sample, &tmin, &tmax)) return 0;
        *measurement = tmin;
        *is_high = 0u;
        return 1;

    case BMS_PARAM_GROUP_DSG_OT:
        if (!sample->current_ma_valid || sample->current_ma <= 0 ||
            !external_temp_extremes(sample, &tmin, &tmax)) return 0;
        *measurement = tmax;
        *is_high = 1u;
        return 1;

    case BMS_PARAM_GROUP_DSG_UT:
        if (!sample->current_ma_valid || sample->current_ma <= 0 ||
            !external_temp_extremes(sample, &tmin, &tmax)) return 0;
        *measurement = tmin;
        *is_high = 0u;
        return 1;

    case BMS_PARAM_GROUP_MOS_OT:
        bms_afe_get_info(&info);
        if ((info.temp_mask & BIT(3)) == 0u) return 0;
        *measurement = sample->temp_dC[3];
        *is_high = 1u;
        return 1;

    case BMS_PARAM_GROUP_CELL_DELTA:
        *measurement = sample->cell_delta_mv;
        *is_high = 1u;
        return sample->cell_count ? 1 : 0;

    case BMS_PARAM_GROUP_SOC_LOW:
        *measurement = soc;
        *is_high = 0u;
        return 1;

    default:
        return 0;
    }
}

static void clear_group(u16 group)
{
    u8 level;
    for (level = 0u; level < BMS_PROTECT_LEVEL_COUNT; ++level) {
        set_level_state(group, level, 0u);
        s_pending[group][level] = 0u;
    }
}

static void update_level(u16 group, u8 level, s32 measure, u8 is_high,
                         s32 threshold, s32 recovery, u32 delay_ms)
{
    u8 active = get_level_state(group, level);
    u8 trigger;
    u8 clear;
    u32 delay_us;

    if (is_high) {
        trigger = (measure >= threshold) ? 1u : 0u;
        clear = (recovery < threshold) ? (measure <= recovery) : (measure < threshold);
    } else {
        trigger = (measure <= threshold) ? 1u : 0u;
        clear = (recovery > threshold) ? (measure >= recovery) : (measure > threshold);
    }

    if (active) {
        if (clear) {
            set_level_state(group, level, 0u);
            s_pending[group][level] = 0u;
        }
        return;
    }

    if (!trigger) {
        s_pending[group][level] = 0u;
        return;
    }

    if (delay_ms == 0u) {
        set_level_state(group, level, 1u);
        s_pending[group][level] = 0u;
        return;
    }

    if (!s_pending[group][level]) {
        s_pending[group][level] = 1u;
        s_pending_tick[group][level] = clock_time();
        return;
    }

    delay_us = delay_ms * 1000u;
    if (clock_time_exceed(s_pending_tick[group][level], delay_us)) {
        set_level_state(group, level, 1u);
        s_pending[group][level] = 0u;
    }
}

static void update_group(u16 group, const bms_afe_sample_t *sample, u16 soc)
{
    s32 measure;
    s32 recovery;
    s32 delay;
    s32 threshold;
    u8 is_high;
    u8 level;

    if (!group_measurement(group, sample, soc, &measure, &is_high) ||
        !get_param_requested(group, BMS_PARAM_FIELD_RECOVERY, &recovery) ||
        !get_param_requested(group, BMS_PARAM_FIELD_FILTER_DELAY, &delay)) {
        clear_group(group);
        return;
    }

    if (delay < 0) delay = 0;
    for (level = 0u; level < BMS_PROTECT_LEVEL_COUNT; ++level) {
        if (!get_param_requested(group, level, &threshold)) {
            set_level_state(group, level, 0u);
            s_pending[group][level] = 0u;
            continue;
        }
        update_level(group, level, measure, is_high, threshold, recovery, (u32)delay);
    }
}

static void update_vetoes(void)
{
    u16 protect_bits = (u16)(s_status.l2_bitmap | s_status.l3_bitmap);
    u16 charge_mask = group_bit(BMS_PARAM_GROUP_CELL_OV) |
                      group_bit(BMS_PARAM_GROUP_CHG_OC) |
                      group_bit(BMS_PARAM_GROUP_CHG_OT) |
                      group_bit(BMS_PARAM_GROUP_CHG_UT) |
                      group_bit(BMS_PARAM_GROUP_MOS_OT);
    u16 discharge_mask = group_bit(BMS_PARAM_GROUP_CELL_UV) |
                         group_bit(BMS_PARAM_GROUP_DSG_OC) |
                         group_bit(BMS_PARAM_GROUP_DSG_OT) |
                         group_bit(BMS_PARAM_GROUP_DSG_UT) |
                         group_bit(BMS_PARAM_GROUP_MOS_OT) |
                         group_bit(BMS_PARAM_GROUP_SOC_LOW);

    s_status.active_bitmap = (u16)(s_status.l1_bitmap |
                                   s_status.l2_bitmap |
                                   s_status.l3_bitmap);
    s_status.charge_veto = (protect_bits & charge_mask) ? 1u : 0u;
    s_status.discharge_veto = (protect_bits & discharge_mask) ? 1u : 0u;
}

static int apply_mos(void)
{
    u8 charge_on;
    u8 discharge_on;
    u8 common_veto;
    int rc;

    charge_on = (s_status.user_charge_on && !s_status.charge_veto) ? 1u : 0u;
    discharge_on = (s_status.user_discharge_on && !s_status.discharge_veto) ? 1u : 0u;

    common_veto = ((s_status.l2_bitmap | s_status.l3_bitmap) &
                   group_bit(BMS_PARAM_GROUP_MOS_OT)) ? 1u : 0u;

#if (BMS_PROTECT_OPPOSITE_REOPEN_ENABLE)
    if (!common_veto && s_last_current_valid) {
        if (s_last_current_ma > 0 && s_status.user_charge_on)
            charge_on = 1u;
        else if (s_last_current_ma < 0 && s_status.user_discharge_on)
            discharge_on = 1u;
    }
#endif

    s_status.effective_charge_on = charge_on;
    s_status.effective_discharge_on = discharge_on;

    if (!s_have_sample && (charge_on || discharge_on))
        return 1;

    if (s_applied_charge == charge_on && s_applied_discharge == discharge_on)
        return 1;

    rc = bms_afe_set_mos(charge_on, discharge_on);
    s_status.last_mos_error = (s16)rc;
    if (rc != 0) return 0;

    s_applied_charge = charge_on;
    s_applied_discharge = discharge_on;
    return 1;
}

void bms_protect_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(s_pending, 0, sizeof(s_pending));
    memset(s_pending_tick, 0, sizeof(s_pending_tick));
    s_status.user_charge_on = 1u;
    s_status.user_discharge_on = 1u;
    s_applied_charge = 0xFFu;
    s_applied_discharge = 0xFFu;
    s_have_sample = 0u;
    s_last_current_valid = 0u;
    s_last_current_ma = 0;
}

void bms_protect_update(const bms_afe_sample_t *sample, u16 soc)
{
    u16 group;

    if (!sample) return;
    s_have_sample = 1u;
    s_last_current_valid = sample->current_ma_valid;
    s_last_current_ma = sample->current_ma;

    for (group = 0u; group < BMS_PARAM_GROUP_COUNT; ++group)
        update_group(group, sample, soc);

    update_vetoes();
    (void)apply_mos();
}

int bms_protect_request_mos(u8 charge_on, u8 discharge_on)
{
    s_status.user_charge_on = charge_on ? 1u : 0u;
    s_status.user_discharge_on = discharge_on ? 1u : 0u;
    return apply_mos();
}

void bms_protect_force_mos_reapply(void)
{
    s_applied_charge = 0xFFu;
    s_applied_discharge = 0xFFu;
    (void)apply_mos();
}

const bms_protect_status_t *bms_protect_get_status(void)
{
    return &s_status;
}
