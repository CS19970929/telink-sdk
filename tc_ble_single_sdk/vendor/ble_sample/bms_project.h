#ifndef BMS_PROJECT_H_
#define BMS_PROJECT_H_

#include "tl_common.h"
#include "bms_afe.h"
#include "bms_param.h"

#define BMS_PROTECT_REG_COUNT BMS_PARAM_COUNT

/*
 * BMS scheduler / BLE low-power coordination.
 *
 * Telink BLE suspend is still allowed, but the BMS layer arms an application
 * wake deadline so BLE connection interval / slave latency can never postpone
 * the next BMS sampling/protection deadline indefinitely.
 */
#ifndef BMS_PM_APP_WAKE_ENABLE
#define BMS_PM_APP_WAKE_ENABLE 1u
#endif

#ifndef BMS_PM_MIN_WAKE_LEAD_US
#define BMS_PM_MIN_WAKE_LEAD_US 1000u
#endif

#ifndef BMS_SCHEDULER_OVERRUN_TOLERANCE_US
#define BMS_SCHEDULER_OVERRUN_TOLERANCE_US 20000u
#endif

typedef struct {
    bms_afe_sample_t afe;
    u16 soc;
    u16 soh;
    u16 capacity_now_x100;
    u16 capacity_full_x100;
    u16 capacity_factory_x100;
    u16 cycle_count;
    u8 mac_public[6];
    u8 afe_init_ok;
    s16 afe_last_error;

    /* Scheduler diagnostics. afe_sample_dt_us is the real elapsed time between
     * AFE sample attempts, not an assumed fixed period. SOC integration can use
     * the same elapsed-time principle when the SOC module is introduced.
     */
    u32 afe_sample_dt_us;
    u32 scheduler_overrun_count;
    u32 pm_app_wakeup_tick;
} bms_project_state_t;

void bms_project_init(void);
void bms_project_process(void);
void bms_project_irq_handler(void);
const bms_project_state_t *bms_project_get_state(void);

u16 bms_project_read_legacy_d000(u16 offset);
u16 bms_project_read_protect(u16 offset);
int bms_project_write_protect(u16 offset, u16 value);
int bms_project_write_protect_block(u16 offset, u16 qty, const u16 *values);
void bms_project_set_soc(u16 soc);
int bms_project_command(u16 value);

#endif
