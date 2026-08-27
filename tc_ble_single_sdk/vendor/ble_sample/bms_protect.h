#ifndef BMS_PROTECT_H_
#define BMS_PROTECT_H_

#include "tl_common.h"
#include "bms_afe.h"

typedef struct {
    u16 l1_bitmap;
    u16 l2_bitmap;
    u16 l3_bitmap;
    u16 active_bitmap;
    u8 user_charge_on;
    u8 user_discharge_on;
    u8 charge_veto;
    u8 discharge_veto;
    u8 effective_charge_on;
    u8 effective_discharge_on;
    s16 last_mos_error;
} bms_protect_status_t;

void bms_protect_init(void);
void bms_protect_update(const bms_afe_sample_t *sample, u16 soc);
int bms_protect_request_mos(u8 charge_on, u8 discharge_on);
void bms_protect_force_mos_reapply(void);
const bms_protect_status_t *bms_protect_get_status(void);

#endif /* BMS_PROTECT_H_ */
