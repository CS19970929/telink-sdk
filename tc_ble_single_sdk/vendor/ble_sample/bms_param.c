#include "bms_param.h"
#include "bms_afe.h"
#include <string.h>

/* Defaults are stored in common physical units, not in AFE register codes.
 * Groups are five fields: L1, L2, L3, recovery, filter/delay.
 */
static const s32 k_defaults[BMS_PARAM_COUNT] = {
    /* 0 CELL_OV: mV, mV, mV, mV, ms */
    4100, 4150, 4200, 4050, 100,
    /* 1 CELL_UV */
    3000, 2900, 2700, 3050, 100,
    /* 2 PACK/BUS_OV (legacy semantics retained) */
    4100, 4150, 4200, 4050, 100,
    /* 3 PACK/BUS_UV */
    3000, 2900, 2700, 3050, 100,
    /* 4 CHG_OC: mA, mA, mA, mA, ms */
    10000, 15000, 20000, 10000, 500,
    /* 5 DSG_OC */
    10000, 15000, 20000, 10000, 500,
    /* 6 CHG_OT: 0.1C, 0.1C, 0.1C, 0.1C, ms */
    400, 500, 550, 500, 100,
    /* 7 CHG_UT */
    50, 30, 0, 50, 100,
    /* 8 DSG_OT */
    500, 550, 600, 500, 100,
    /* 9 DSG_UT */
    -100, -150, -200, -100, 100,
    /* A MOS_OT */
    750, 850, 950, 800, 100,
    /* B CELL_DELTA: mV */
    600, 800, 1000, 800, 100,
    /* C SOC_LOW: percent */
    20, 10, 5, 11, 100,
};

static bms_param_value_t s_values[BMS_PARAM_COUNT];

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

    if (field == 4u) {
        d->unit = BMS_PARAM_UNIT_MS;
        d->min_value = 0;
        d->max_value = 60000;
        return;
    }

    if (group <= 3u) {
        d->unit = BMS_PARAM_UNIT_MV;
        d->min_value = 1000;
        d->max_value = 5000;
        return;
    }

    if (group == 4u || group == 5u) {
        d->unit = BMS_PARAM_UNIT_MA;
        d->min_value = 0;
        d->max_value = 1000000;
        d->step = 100; /* legacy protocol resolution is 0.1 A */
        return;
    }

    if (group >= 6u && group <= 10u) {
        d->unit = BMS_PARAM_UNIT_TEMP_DC;
        d->min_value = -400;
        d->max_value = 1250;
        return;
    }

    if (group == 11u) {
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

    /* The AFE adapter only overrides logical parameters for which it has a
     * direct and unambiguous hardware projection. Other parameters remain in
     * the common model but are not falsely advertised as active protection.
     */
    if (bms_afe_get_param_limits(d->id, &afe_min, &afe_max, &afe_step)) {
        d->min_value = afe_min;
        d->max_value = afe_max;
        d->step = afe_step;
        d->flags |= BMS_PARAM_FLAG_ACTIVE;
        d->enforcement = BMS_PARAM_ENFORCE_AFE;

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

static int set_by_index(u16 index, s32 requested)
{
    bms_param_desc_t d;
    s32 effective;
    int rc;

    if (!prepare_value(index, requested, &effective)) return 0;
    if (!bms_param_get_desc(index, &d)) return 0;

    if ((d.flags & BMS_PARAM_FLAG_ACTIVE) &&
        (d.enforcement == BMS_PARAM_ENFORCE_AFE || d.enforcement == BMS_PARAM_ENFORCE_HYBRID)) {
        rc = bms_afe_apply_param(d.id, effective);
        if (rc != BMS_AFE_APPLY_OK) return 0;
    }

    s_values[index].requested_value = requested;
    s_values[index].effective_value = effective;
    return 1;
}

void bms_param_init(void)
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

int bms_param_apply_hardware_all(void)
{
    u16 i;
    bms_param_desc_t d;
    int rc;

    for (i = 0; i < BMS_PARAM_COUNT; ++i) {
        if (!bms_param_get_desc(i, &d)) continue;
        if ((d.flags & BMS_PARAM_FLAG_ACTIVE) == 0u) continue;
        if (d.enforcement != BMS_PARAM_ENFORCE_AFE &&
            d.enforcement != BMS_PARAM_ENFORCE_HYBRID) continue;

        rc = bms_afe_apply_param(d.id, s_values[i].effective_value);
        if (rc != BMS_AFE_APPLY_OK) return -(int)(i + 1u);
    }
    return 0;
}

static s32 legacy_to_common(u16 offset, u16 raw)
{
    u16 group = param_group(offset);
    u16 field = param_field(offset);

    if (field == 4u) return (s32)raw;
    if (group == 4u || group == 5u) return (s32)raw * 100; /* 0.1A -> mA */
    if (group >= 6u && group <= 10u) return (s32)raw - 400; /* old +40C offset */
    return (s32)raw;
}

static u16 common_to_legacy(u16 offset, s32 value)
{
    u16 group = param_group(offset);
    u16 field = param_field(offset);
    s32 raw;

    if (field == 4u)
        raw = value;
    else if (group == 4u || group == 5u)
        raw = value / 100;
    else if (group >= 6u && group <= 10u)
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
    /* Legacy window preserves the requested value. The new capability block
     * separately exposes the effective hardware-quantized value.
     */
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
    s32 effective;

    if (!raw_values || qty == 0u || offset >= BMS_PARAM_COUNT ||
        (u32)offset + qty > BMS_PARAM_COUNT) return 0;

    /* Validate the complete block first so a range/unit error cannot leave a
     * half-written block. Hardware-I/O failure rollback is intentionally left
     * to the future persistent transaction layer.
     */
    for (i = 0; i < qty; ++i) {
        if (!prepare_value((u16)(offset + i),
                           legacy_to_common((u16)(offset + i), raw_values[i]),
                           &effective))
            return 0;
    }

    for (i = 0; i < qty; ++i) {
        if (!set_by_index((u16)(offset + i),
                          legacy_to_common((u16)(offset + i), raw_values[i])))
            return 0;
    }
    return 1;
}

static u16 hi16(s32 value)
{
    return (u16)(((u32)value) >> 16);
}

static u16 lo16(s32 value)
{
    return (u16)((u32)value);
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
    default: return 0u;
    }
}

u16 bms_param_read_cap_reg(u16 reg)
{
    u16 rel;
    u16 index;
    u16 field;
    bms_param_desc_t d;
    bms_param_value_t v;
    u16 packed_flags;

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
