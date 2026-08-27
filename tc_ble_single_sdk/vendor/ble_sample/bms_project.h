#ifndef BMS_PROJECT_H_
#define BMS_PROJECT_H_

#include "tl_common.h"
#include "sh3673510.h"

#define BMS_PROTECT_REG_COUNT 65u

typedef struct {
    sh3673510_sample_t afe;
    u16 soc;
    u16 soh;
    u16 capacity_now_x100;
    u16 capacity_full_x100;
    u16 capacity_factory_x100;
    u16 cycle_count;
    u16 protect[BMS_PROTECT_REG_COUNT];
    u8 afe_init_ok;
    s16 afe_last_error;
} bms_project_state_t;

void bms_project_init(void);
void bms_project_process(void);
void bms_project_irq_handler(void);
const bms_project_state_t *bms_project_get_state(void);

u16 bms_project_read_legacy_d000(u16 offset);
u16 bms_project_read_protect(u16 offset);
int bms_project_write_protect(u16 offset, u16 value);
void bms_project_set_soc(u16 soc);
int bms_project_command(u16 value);

#endif
