#ifndef BMS_AFE_H_
#define BMS_AFE_H_

#include "tl_common.h"

#define BMS_AFE_MAX_CELLS          20u
#define BMS_AFE_TEMP_CHANNELS      4u

#define BMS_AFE_TYPE_UNKNOWN       0x0000u
#define BMS_AFE_TYPE_SH3673510     0x3510u
#define BMS_AFE_TYPE_SIMULATED     0xFFFFu

#define BMS_AFE_FEAT_CELL_OV       BIT(0)
#define BMS_AFE_FEAT_CELL_UV       BIT(1)
#define BMS_AFE_FEAT_OCD1          BIT(2)
#define BMS_AFE_FEAT_OCD2          BIT(3)
#define BMS_AFE_FEAT_SHORT         BIT(4)
#define BMS_AFE_FEAT_OCC           BIT(5)
#define BMS_AFE_FEAT_TEMP          BIT(6)
#define BMS_AFE_FEAT_BALANCE       BIT(7)
#define BMS_AFE_FEAT_MOS_CONTROL   BIT(8)
#define BMS_AFE_FEAT_RAW_DEBUG     BIT(9)

#define BMS_AFE_FAULT_CELL_OV      BIT(0)
#define BMS_AFE_FAULT_CELL_UV      BIT(1)
#define BMS_AFE_FAULT_CHG_OC       BIT(2)
#define BMS_AFE_FAULT_DSG_OC1      BIT(3)
#define BMS_AFE_FAULT_DSG_OC2      BIT(4)
#define BMS_AFE_FAULT_SHORT        BIT(5)
#define BMS_AFE_FAULT_CHG_OT       BIT(6)
#define BMS_AFE_FAULT_CHG_UT       BIT(7)
#define BMS_AFE_FAULT_DSG_OT       BIT(8)
#define BMS_AFE_FAULT_DSG_UT       BIT(9)
#define BMS_AFE_FAULT_WDT          BIT(10)
#define BMS_AFE_FAULT_RESET        BIT(11)
#define BMS_AFE_FAULT_ALL          0xFFFFFFFFu

#define BMS_AFE_APPLY_OK            0
#define BMS_AFE_APPLY_NOT_MAPPED    1

typedef struct {
    u16 afe_type;
    u8 cell_count;
    u8 temp_mask;
    u32 feature_bits;
} bms_afe_info_t;

typedef struct {
    u8 cell_count;
    u16 cell_mv[BMS_AFE_MAX_CELLS];
    u16 cell_min_mv;
    u16 cell_max_mv;
    u16 cell_delta_mv;
    u32 pack_mv;

    s16 current_raw;
    s32 current_ma;
    u8 current_ma_valid;

    u16 ts_raw[BMS_AFE_TEMP_CHANNELS];
    u32 ts_ohm[BMS_AFE_TEMP_CHANNELS];
    s16 temp_dC[BMS_AFE_TEMP_CHANNELS];

    u16 vtop_mv;
    u16 vchgr_mv;
    u32 fault_bits;

    /* Raw AFE status is intentionally retained for engineering diagnostics.
     * Application logic must not decode these fields outside the AFE adapter.
     */
    u8 flag1;
    u8 flag2;
    u8 flag3;
    u8 bstatus1;
    u8 bstatus2;
    u8 online;
} bms_afe_sample_t;

void bms_afe_get_info(bms_afe_info_t *info);
int bms_afe_init(void);
int bms_afe_sample(bms_afe_sample_t *sample);
int bms_afe_set_mos(u8 charge_on, u8 discharge_on);
int bms_afe_clear_faults(u32 fault_mask);

/* Return 1 when this logical parameter has a direct AFE hardware projection.
 * min/max/step are expressed in the logical parameter's standard unit.
 */
int bms_afe_get_param_limits(u16 param_id, s32 *min_value, s32 *max_value, s32 *step);

/* value is already range-checked and safely quantized by bms_param. */
int bms_afe_apply_param(u16 param_id, s32 value);

#endif /* BMS_AFE_H_ */
