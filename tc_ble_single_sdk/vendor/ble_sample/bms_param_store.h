#ifndef BMS_PARAM_STORE_H_
#define BMS_PARAM_STORE_H_

#include "tl_common.h"

/* Store status bits exposed to diagnostics. */
#define BMS_PARAM_STORE_ST_SUPPORTED       BIT(0)
#define BMS_PARAM_STORE_ST_VALID_RECORD    BIT(1)
#define BMS_PARAM_STORE_ST_LAST_SAVE_OK    BIT(2)
#define BMS_PARAM_STORE_ST_ERROR           BIT(3)
#define BMS_PARAM_STORE_ST_ACTIVE_SLOT_B   BIT(4)

int bms_param_store_load(s32 *values, u16 count, u16 schema_version);
int bms_param_store_save(const s32 *values, u16 count, u16 schema_version);
u16 bms_param_store_status_word(void);

#endif /* BMS_PARAM_STORE_H_ */
