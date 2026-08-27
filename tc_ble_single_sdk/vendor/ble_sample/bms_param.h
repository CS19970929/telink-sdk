#ifndef BMS_PARAM_H_
#define BMS_PARAM_H_

#include "tl_common.h"

#define BMS_PARAM_GROUP_COUNT          13u
#define BMS_PARAM_FIELDS_PER_GROUP     5u
#define BMS_PARAM_COUNT                (BMS_PARAM_GROUP_COUNT * BMS_PARAM_FIELDS_PER_GROUP)

#define BMS_PARAM_LEGACY_BASE          0x2100u
#define BMS_PARAM_META_BASE            0x2000u
#define BMS_PARAM_META_COUNT           12u
#define BMS_PARAM_CAP_BASE             0x4000u
#define BMS_PARAM_CAP_STRIDE           14u
#define BMS_PARAM_CAP_REG_COUNT        (BMS_PARAM_COUNT * BMS_PARAM_CAP_STRIDE)

#define BMS_PARAM_PROTOCOL_VERSION     0x0200u
#define BMS_PARAM_SCHEMA_VERSION       0x0001u
#define BMS_PARAM_META_MAGIC           0x424Du /* 'BM' */

/* Stable logical ID: 0x10GF, G=group, F=field.
 * Group semantics are independent from AFE register maps.
 */
#define BMS_PARAM_ID(group, field)     ((u16)(0x1000u + ((u16)(group) << 4) + (u16)(field)))

#define BMS_PARAM_ID_CELL_OV_L3        BMS_PARAM_ID(0u, 2u)
#define BMS_PARAM_ID_CELL_UV_L3        BMS_PARAM_ID(1u, 2u)

typedef enum {
    BMS_PARAM_UNIT_NONE = 0,
    BMS_PARAM_UNIT_MV,
    BMS_PARAM_UNIT_MS,
    BMS_PARAM_UNIT_MA,
    BMS_PARAM_UNIT_TEMP_DC,     /* signed 0.1 degC */
    BMS_PARAM_UNIT_PERCENT,
} bms_param_unit_t;

typedef enum {
    BMS_PARAM_ENFORCE_NONE = 0,
    BMS_PARAM_ENFORCE_SOFTWARE = 1,
    BMS_PARAM_ENFORCE_AFE = 2,
    BMS_PARAM_ENFORCE_HYBRID = 3,
} bms_param_enforcement_t;

typedef enum {
    BMS_PARAM_QUANTIZE_NEAREST = 0,
    BMS_PARAM_QUANTIZE_FLOOR = 1,
    BMS_PARAM_QUANTIZE_CEIL = 2,
} bms_param_quantize_t;

#define BMS_PARAM_FLAG_SUPPORTED       BIT(0)
#define BMS_PARAM_FLAG_READABLE        BIT(1)
#define BMS_PARAM_FLAG_WRITABLE        BIT(2)
#define BMS_PARAM_FLAG_ACTIVE          BIT(3)

typedef struct {
    u16 id;
    u16 legacy_offset;
    u8 unit;
    u8 flags;
    u8 enforcement;
    u8 quantize;
    s32 min_value;
    s32 max_value;
    s32 step;
    s32 default_value;
} bms_param_desc_t;

typedef struct {
    s32 requested_value;
    s32 effective_value;
} bms_param_value_t;

void bms_param_init(void);
int bms_param_apply_hardware_all(void);

u16 bms_param_read_legacy(u16 offset);
int bms_param_write_legacy(u16 offset, u16 raw_value);
int bms_param_write_legacy_block(u16 offset, u16 qty, const u16 *raw_values);

int bms_param_get_desc(u16 index, bms_param_desc_t *desc);
int bms_param_get_value(u16 index, bms_param_value_t *value);

u16 bms_param_read_meta_reg(u16 reg);
u16 bms_param_read_cap_reg(u16 reg);

#endif /* BMS_PARAM_H_ */
