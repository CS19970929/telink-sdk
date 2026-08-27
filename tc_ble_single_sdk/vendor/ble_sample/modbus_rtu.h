#ifndef MODBUS_RTU_H_
#define MODBUS_RTU_H_

#include "tl_common.h"

#define MODBUS_SLAVE_ADDR              0x01u

#define BMS_REALTIME_REG_BASE          0xD120u
#define BMS_REALTIME_REG_COUNT         11u
#define BMS_REALTIME_REG_MAGIC         0x4253u
#define BMS_REALTIME_REG_VERSION       0x0001u

#define BMS_PROTECT_STATUS_REG_BASE    0xD130u
#define BMS_PROTECT_STATUS_REG_COUNT   11u
#define BMS_PROTECT_STATUS_MAGIC       0x5052u /* 'PR' */
#define BMS_PROTECT_STATUS_VERSION     0x0001u

#define PROD_SN_REG_BASE               0xC002u
#define PROD_SN_REG_COUNT              16u
#define PROD_HW_VER_REG_BASE           (PROD_SN_REG_BASE + PROD_SN_REG_COUNT)
#define PROD_HW_VER_REG_COUNT          16u
#define PROD_SW_VER_REG_BASE           (PROD_HW_VER_REG_BASE + PROD_HW_VER_REG_COUNT)
#define PROD_SW_VER_REG_COUNT          16u

int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);
u16 modbus_crc16(const u8 *data, u32 len);

#endif
