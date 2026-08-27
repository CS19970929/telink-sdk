#include "bms_param.h"
#include "bms_afe.h"
#include "bms_param_store.h"
#include <string.h>

#define BMS_PARAM_SAVE_DELAY_US        1500000u

/* Defaults are stored in common physical units, never in AFE register codes.
 * Every group uses: L1, L2, L3, recovery, filter/delay.
 */
static const s32 k_defaults[BMS_PARAM_COUNT] = {
    /* CELL_OV: mV, mV, mV, mV, ms */
    4100, 4150, 4200, 4050, 100,
    /* CELL_UV */
    3000, 2900, 2700, 3050, 100,
    /* BUS_OV - legacy semantics retained; currently not enforced */
    4100, 4150, 4200, 4050, 100,
    /* BUS_UV - legacy semantics retained; currently not enforced */
    3000, 2900, 2700, 3050, 100,
    /* CHG_OC: mA, mA, mA, mA, ms */
    10000, 15000, 20000, 10000, 500,
    /* DSG_OC */
    10000, 15000, 20000, 10000, 500,
    /* CHG_OT: signed 0.1C */
    400, 500, 550, 500, 100,
    /* CHG_UT */
    50, 30, 0, 50, 100,
    /* DSG_OT */
    500, 550, 600, 500, 100,
    /* DSG_UT */
    -100, -150, -200, -100, 100,
    /* MOS_OT */
    750, 850, 950, 800, 100,
    /* CELL_DELTA: mV */
    600, 800, 1000, 800, 100,
    /* SOC_LOW: percent */
    20, 10, 5, 11, 100,
};

static bms_param_value_t s_values[BMS_PARAM_COUNT];

/* Static transaction/persistence scratch keeps the TC32 stack small. Parameter
 * writes are serialized by the bare-metal main loop / Modbus dispatcher.
 */
static s32 s_stage_requested[BMS_PARAM_COUNT];
static s32 s_stage_effective[BMS_PARAM_COUNT];
static u8 s_dirty;
static u8 s_restore_accepted;
static u8 s_restore_rejected;
static u32 s_dirty_tick;

static u16 param_group(u16 index)
{
    return (u16)(index / BMS_PARAM_FIELDS_PER_GROUP);
}

static u16 param_field(u16 index)
{
    return (u16)(index % BMS_PARAM_FIELDS_PER_GROUP);
}

static void generic_limits(u16 group, u16 field, bms_param_desc_t *d)
{
    d->step = 1;

    if (field == BMS_PARAM_FIELD_FILTER_DELAY) {
        d->unit = BMS_PARAM_UNIT_MS;
        d->min_value = 0;
        d->max_value = 60000;
        return;
    }

    if (group <= BMS_PARAM_GROUP_BUS_UV) {
        d->unit = BMS_PARAM_UNIT_MV;
        d->min_value = 1000;
        d->max_value = 5000;
        return;
    }

    if (group == BMS_PARAM_GROUP_CHG_OC || group == BMS_PARAM_GROUP_DSG_OC) {
        d->unit = BMS_PARAM_UNIT_MA;
        d->min_value = 0;
        d->max_value = 1000000;
        d->step = 100; /* legacy transport resolution is 0.1 A */
        return;
    }

    if (group >= BMS_PARAM_GROUP_CHG_OT && group <= BMS_PARAM_GROUP_MOS_OT) {
        d->unit = BMS_PARAM_UNIT_TEMP_DC;
        d->min_value = -400;
        d->max_value = 1250;
        return;
    }

    if (group == BMS_PARAM_GROUP_CELL_DELTA) {
        d->unit = BMS_PARAM_UNIT_MV;
        d->min_value = 0;
        d->max_value = 5000;
        return;
    }

    d->unit = BMS_PARAM_UNIT_PERCENT;
    d->min_value = 0;
    d->max_value = 100;
}

int bms_param_get_desc(u16 index, bms_param_desc_t *d)
{
    u16 group;
    u16 field;
    s32 afe_min;
    s32 afe_max;
    s32 afe_step;

    if (!d || index >= BMS_PARAM_COUNT) return 0;

    memset(d, 0, sizeof(*d));
    group = param_group(index);
    field = param_field(index);

    d->id = BMS_PARAM_ID(group, field);
    d->legacy_offset = index;
    d->flags = BMS_PARAM_FLAG_SUPPORTED |
               BMS_PARAM_FLAG_READABLE |
               BMS_PARAM_FLAG_WRITABLE;
    d->enforcement = BMS_PARAM_ENFORCE_NONE;
    d->quantize = BMS_PARAM_QUANTIZE_NEAREST;
    d->default_value = k_defaults[index];
    generic_limits(group, field, d);

    /* BUS_OV/BUS_UV legacy fields do not yet have an unambiguous product-level
     * definition in this project, so keep them configurable but do not claim
     * that they are enforced. Every other common group is enforced in software.
     */
    if (group != BMS_PARAM_GROUP_BUS_OV && group != BMS_PARAM_GROUP_BUS_UV) {
        d->flags |= BMS_PARAM_FLAG_ACTIVE;
        d->enforcement = BMS_PARAM_ENFORCE_SOFTWARE;
    }

    /* A direct AFE projection augments, rather than replaces, software policy.
     * The adapter owns AFE-specific ranges/steps. For this board that currently
     * means only CELL_OV_L3 and CELL_UV_L3.
     */
    if (bms_afe_get_param_limits(d->id, &afe_min, &afe_max, &afe_step)) {
        d->min_value = afe_min;
        d->max_value = afe_max;
        d->step = afe_step;
        d->flags |= BMS_PARAM_FLAG_ACTIVE;
        d->enforcement = BMS_PARAM_ENFORCE_HYBRID;

        /* Hardware protection is quantized in the conservative direction. */
        if (d->id == BMS_PARAM_ID_CELL_OV_L3)
            d->quantize = BMS_PARAM_QUANTIZE_FLOOR;
        else if (d->id == BMS_PARAM_ID_CELL_UV_L3)
            d->quantize = BMS_PARAM_QUANTIZE_CEIL;
    }

    return 1;
}

int bms_param_get_value(u16 index, bms_param_value_t *value)
{
    if (!value || index >= BMS_PARAM_COUNT) return 0;
    *value = s_values[index];
    return 1;
}

static s32 quantize_value(const bms_param_desc_t *d, s32 value)
{
    s32 delta;
    s32 q;
    s32 rem;

    if (!d || d->step <= 1) return value;

    /* prepare_value() guarantees value >= min, so delta/rem are non-negative. */
    delta = value - d->min_value;
    q = delta / d->step;
    rem = delta % d->step;

    if (d->quantize == BMS_PARAM_QUANTIZE_CEIL && rem != 0)
        ++q;
    else if (d->quantize == BMS_PARAM_QUANTIZE_NEAREST && rem >= (d->step / 2))
        ++q;

    value = d->min_value + q * d->step;
    if (value < d->min_value) value = d->min_value;
    if (value > d->max_value) value = d->max_value;
    return value;
}

static int prepare_value(u16 index, s32 requested, s32 *effective)
{
    bms_param_desc_t d;

    if (!effective || !bms_param_get_desc(index, &d)) return 0;
    if ((d.flags & BMS_PARAM_FLAG_WRITABLE) == 0u) return 0;
    if (requested < d.min_value || requested > d.max_value) return 0;

    *effective = quantize_value(&d, requested);
    return 1;
}

static int desc_has_afe_projection(const bms_param_desc_t *d)
{
    if (!d) return 0;
    if ((d->flags & BMS_PARAM_FLAG_ACTIVE) == 0u) return 0;
    return d->enforcement == BMS_PARAM_ENFORCE_AFE ||
           d->enforcement == BMS_PARAM_ENFORCE_HYBRID;
}

static void mark_dirty(void)
{
    s_dirty = 1u;
    s_dirty_tick = clock_time();
}

/* Two-phase parameter transaction:
 * 1) validate/quantize every logical value;
 * 2) project all applicable values to hardware while RAM DB still contains the
 *    old configuration; if an AFE write fails, previous AFE writes are rolled
 *    back from that old DB;
 * 3) commit requested/effective values to RAM only after hardware succeeds.
 */
static int commit_staged(u16 start_index, u16 count)
{
    u16 i;
    u16 j;
    u16 index;
    u8 changed = 0u;
    bms_param_desc_t d;
    int rc;

    if (count == 0u || start_index >= BMS_PARAM_COUNT ||
        (u32)start_index + count > BMS_PARAM_COUNT) return 0;

    for (i = 0; i < count; ++i) {
        index = (u16)(start_index + i);
        if (!prepare_value(index, s_stage_requested[i], &s_stage_effective[i]))
            return 0;
        if (s_values[index].requested_value != s_stage_requested[i] ||
            s_values[index].effective_value != s_stage_effective[i])
            changed = 1u;
    }

    for (i = 0; i < count; ++i) {
        index = (u16)(start_index + i);
        if (!bms_param_get_desc(index, &d)) return 0;
        if (!desc_has_afe_projection(&d)) continue;

        rc = bms_afe_apply_param(d.id, s_stage_effective[i]);
        if (rc != BMS_AFE_APPLY_OK) {
            /* Best-effort rollback of every earlier AFE projection. RAM values
             * are still old, so they are the authoritative rollback source.
             */
            for (j = 0; j < i; ++j) {
                u16 rollback_index = (u16)(start_index + j);
                if (!bms_param_get_desc(rollback_index, &d)) continue;
                if (!desc_has_afe_projection(&d)) continue;
                (void)bms_afe_apply_param(d.id, s_values[rollback_index].effective_value);
            }
            return 0;
        }
    }

    for (i = 0; i < count; ++i) {
        index = (u16)(start_index + i);
        s_values[index].requested_value = s_stage_requested[i];
        s_values[index].effective_value = s_stage_effective[i];
    }

    if (changed) mark_dirty();
    return 1;
}

static int set_by_index(u16 index, s32 requested)
{
    if (index >= BMS_PARAM_COUNT) return 0;
    s_stage_requested[0] = requested;
    return commit_staged(index, 1u);
}

static void load_defaults(void)
{
    u16 i;
    bms_param_desc_t d;

    memset(s_values, 0, sizeof(s_values));
    for (i = 0; i < BMS_PARAM_COUNT; ++i) {
        if (!bms_param_get_desc(i, &d)) continue;
        s_values[i].requested_value = d.default_value;
        s_values[i].effective_value = quantize_value(&d, d.default_value);
    }
}

void bms_param_init(void)
{
    u16 i;
    s32 effective;
    int loaded;

    s_dirty = 0u;
    s_restore_accepted = 0u;
    s_restore_rejected = 0u;
    s_dirty_tick = clock_time();
    load_defaults();

    loaded = bms_param_store_load(s_stage_requested, BMS_PARAM_COUNT,
                                  BMS_PARAM_SCHEMA_VERSION);
    if (!loaded) return;

    /* A persisted record is accepted atomically only if every value still fits
     * the current schema/AFE capability. This prevents a firmware/AFE change
     * from partially restoring an incompatible protection set.
     */
    for (i = 0; i < BMS_PARAM_COUNT; ++i) {
        if (!prepare_value(i, s_stage_requested[i], &s_stage_effective[i])) {
            s_restore_rejected = 1u;
            return;
        }
    }

    for (i = 0; i < BMS_PARAM_COUNT; ++i) {
        effective = s_stage_effective[i];
        s_values[i].requested_value = s_stage_requested[i];
        s_values[i].effective_value = effective;
    }
    s_restore_accepted = 1u;
}

void bms_param_process(void)
{
    u16 i;

    if (!s_dirty) return;
    if (!clock_time_exceed(s_dirty_tick, BMS_PARAM_SAVE_DELAY_US)) return;

    for (i = 0; i < BMS_PARAM_COUNT; ++i)
        s_stage_requested[i] = s_values[i].requested_value;

    if (bms_param_store_save(s_stage_requested, BMS_PARAM_COUNT,
                             BMS_PARAM_SCHEMA_VERSION)) {
        s_dirty = 0u;
    } else {
        /* Retry later; never spin on a flash failure in the main loop. */
        s_dirty_tick = clock_time();
    }
}

u16 bms_param_persist_status_word(void)
{
    u16 st = bms_param_store_status_word();
    if (s_dirty) st |= BMS_PARAM_PERSIST_ST_DIRTY;
    if (s_restore_accepted) st |= BMS_PARAM_PERSIST_ST_RESTORE_ACCEPTED;
    if (s_restore_rejected) st |= BMS_PARAM_PERSIST_ST_RESTORE_REJECTED;
    return st;
}

int bms_param_apply_hardware_all(void)
{
    u16 i;
    bms_param_desc_t d;
    int rc;

    for (i = 0; i < BMS_PARAM_COUNT; ++i) {
        if (!bms_param_get_desc(i, &d)) continue;
        if (!desc_has_afe_projection(&d)) continue;

        rc = bms_afe_apply_param(d.id, s_values[i].effective_value);
        if (rc != BMS_AFE_APPLY_OK) return -(int)(i + 1u);
    }
    return 0;
}

static s32 legacy_to_common(u16 offset, u16 raw)
{
    u16 group = param_group(offset);
    u16 field = param_field(offset);

    if (field == BMS_PARAM_FIELD_FILTER_DELAY) return (s32)raw;
    if (group == BMS_PARAM_GROUP_CHG_OC || group == BMS_PARAM_GROUP_DSG_OC)
        return (s32)raw * 100; /* legacy A*10 -> common mA */
    if (group >= BMS_PARAM_GROUP_CHG_OT && group <= BMS_PARAM_GROUP_MOS_OT)
        return (s32)raw - 400; /* legacy (degC+40)*10 -> signed 0.1C */
    return (s32)raw;
}

static u16 common_to_legacy(u16 offset, s32 value)
{
    u16 group = param_group(offset);
    u16 field = param_field(offset);
    s32 raw;

    if (field == BMS_PARAM_FIELD_FILTER_DELAY)
        raw = value;
    else if (group == BMS_PARAM_GROUP_CHG_OC || group == BMS_PARAM_GROUP_DSG_OC)
        raw = value / 100;
    else if (group >= BMS_PARAM_GROUP_CHG_OT && group <= BMS_PARAM_GROUP_MOS_OT)
        raw = value + 400;
    else
        raw = value;

    if (raw < 0) raw = 0;
    if (raw > 65535) raw = 65535;
    return (u16)raw;
}

u16 bms_param_read_legacy(u16 offset)
{
    if (offset >= BMS_PARAM_COUNT) return 0u;
    return common_to_legacy(offset, s_values[offset].requested_value);
}

int bms_param_write_legacy(u16 offset, u16 raw_value)
{
    if (offset >= BMS_PARAM_COUNT) return 0;
    return set_by_index(offset, legacy_to_common(offset, raw_value));
}

int bms_param_write_legacy_block(u16 offset, u16 qty, const u16 *raw_values)
{
    u16 i;

    if (!raw_values || qty == 0u || offset >= BMS_PARAM_COUNT ||
        (u32)offset + qty > BMS_PARAM_COUNT) return 0;

    for (i = 0; i < qty; ++i)
        s_stage_requested[i] = legacy_to_common((u16)(offset + i), raw_values[i]);

    return commit_staged(offset, qty);
}

static u16 hi16(s32 value)
{
    return (u16)(((u32)value) >> 16);
}

static u16 lo16(s32 value)
{
    return (u16)((u32)value);
}

static s32 pair_to_s32(u16 hi, u16 lo)
{
    u32 raw = ((u32)hi << 16) | lo;
    return (s32)raw;
}

u16 bms_param_read_value_reg(u16 reg)
{
    u16 rel;
    u16 index;
    u16 word;
    s32 value;

    if (reg >= BMS_PARAM_VALUE_BASE &&
        reg < BMS_PARAM_VALUE_BASE + BMS_PARAM_VALUE_REG_COUNT) {
        rel = (u16)(reg - BMS_PARAM_VALUE_BASE);
        index = (u16)(rel / BMS_PARAM_VALUE_STRIDE);
        word = (u16)(rel % BMS_PARAM_VALUE_STRIDE);
        value = s_values[index].requested_value;
        return word == 0u ? hi16(value) : lo16(value);
    }

    if (reg >= BMS_PARAM_EFFECTIVE_BASE &&
        reg < BMS_PARAM_EFFECTIVE_BASE + BMS_PARAM_VALUE_REG_COUNT) {
        rel = (u16)(reg - BMS_PARAM_EFFECTIVE_BASE);
        index = (u16)(rel / BMS_PARAM_VALUE_STRIDE);
        word = (u16)(rel % BMS_PARAM_VALUE_STRIDE);
        value = s_values[index].effective_value;
        return word == 0u ? hi16(value) : lo16(value);
    }

    return 0u;
}

int bms_param_write_value_block(u16 reg, u16 qty, const u16 *words)
{
    u16 rel;
    u16 start_index;
    u16 count;
    u16 i;

    if (!words || qty == 0u ||
        reg < BMS_PARAM_VALUE_BASE ||
        reg >= BMS_PARAM_VALUE_BASE + BMS_PARAM_VALUE_REG_COUNT)
        return 0;

    rel = (u16)(reg - BMS_PARAM_VALUE_BASE);

    /* A common value is atomic signed32: FC16 must begin at its high word and
     * contain complete high/low pairs. Half-value writes are rejected.
     */
    if ((rel % BMS_PARAM_VALUE_STRIDE) != 0u ||
        (qty % BMS_PARAM_VALUE_STRIDE) != 0u)
        return 0;

    start_index = (u16)(rel / BMS_PARAM_VALUE_STRIDE);
    count = (u16)(qty / BMS_PARAM_VALUE_STRIDE);
    if ((u32)start_index + count > BMS_PARAM_COUNT) return 0;

    for (i = 0; i < count; ++i)
        s_stage_requested[i] = pair_to_s32(words[i * 2u], words[i * 2u + 1u]);

    return commit_staged(start_index, count);
}

u16 bms_param_read_meta_reg(u16 reg)
{
    bms_afe_info_t info;
    u16 off;

    if (reg < BMS_PARAM_META_BASE || reg >= BMS_PARAM_META_BASE + BMS_PARAM_META_COUNT)
        return 0u;

    bms_afe_get_info(&info);
    off = (u16)(reg - BMS_PARAM_META_BASE);
    switch (off) {
    case 0: return BMS_PARAM_META_MAGIC;
    case 1: return BMS_PARAM_PROTOCOL_VERSION;
    case 2: return BMS_PARAM_SCHEMA_VERSION;
    case 3: return info.afe_type;
    case 4: return info.cell_count;
    case 5: return info.temp_mask;
    case 6: return BMS_PARAM_COUNT;
    case 7: return BMS_PARAM_LEGACY_BASE;
    case 8: return BMS_PARAM_CAP_BASE;
    case 9: return BMS_PARAM_CAP_STRIDE;
    case 10: return (u16)info.feature_bits;
    case 11: return (u16)(info.feature_bits >> 16);
    case 12: return BMS_PARAM_VALUE_BASE;
    case 13: return BMS_PARAM_EFFECTIVE_BASE;
    case 14: return BMS_PARAM_VALUE_STRIDE;
    case 15: return BMS_PARAM_VALUE_VERSION;
    default: return 0u;
    }
}

u16 bms_param_read_cap_reg(u16 reg)
{
    u16 rel;
    u16 index;
    u16 field;
    u16 packed_flags;
    bms_param_desc_t d;
    bms_param_value_t v;

    if (reg < BMS_PARAM_CAP_BASE || reg >= BMS_PARAM_CAP_BASE + BMS_PARAM_CAP_REG_COUNT)
        return 0u;

    rel = (u16)(reg - BMS_PARAM_CAP_BASE);
    index = (u16)(rel / BMS_PARAM_CAP_STRIDE);
    field = (u16)(rel % BMS_PARAM_CAP_STRIDE);
    if (!bms_param_get_desc(index, &d) || !bms_param_get_value(index, &v)) return 0u;

    packed_flags = (u16)d.flags |
                   ((u16)(d.enforcement & 0x03u) << 4) |
                   ((u16)(d.quantize & 0x03u) << 6);

    switch (field) {
    case 0: return d.id;
    case 1: return (u16)(BMS_PARAM_LEGACY_BASE + d.legacy_offset);
    case 2: return packed_flags;
    case 3: return d.unit;
    case 4: return hi16(d.min_value);
    case 5: return lo16(d.min_value);
    case 6: return hi16(d.max_value);
    case 7: return lo16(d.max_value);
    case 8: return hi16(d.step);
    case 9: return lo16(d.step);
    case 10: return hi16(v.requested_value);
    case 11: return lo16(v.requested_value);
    case 12: return hi16(v.effective_value);
    case 13: return lo16(v.effective_value);
    default: return 0u;
    }
}
