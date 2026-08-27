#ifndef APP_ATT_H_
#define APP_ATT_H_

#include "tl_common.h"
#include "btname_modbus.h"

typedef enum
{
    ATT_H_START = 0,

    /* GAP */
    GenericAccess_PS_H,
    GenericAccess_DeviceName_CD_H,
    GenericAccess_DeviceName_DP_H,
    GenericAccess_Appearance_CD_H,
    GenericAccess_Appearance_DP_H,
    CONN_PARAM_CD_H,
    CONN_PARAM_DP_H,

    /* GATT */
    GenericAttribute_PS_H,
    GenericAttribute_ServiceChanged_CD_H,
    GenericAttribute_ServiceChanged_DP_H,
    GenericAttribute_ServiceChanged_CCB_H,

    /* Device Information */
    DeviceInformation_PS_H,
    DeviceInformation_pnpID_CD_H,
    DeviceInformation_pnpID_DP_H,

    /* Battery Service */
    BATT_PS_H,
    BATT_LEVEL_INPUT_CD_H,
    BATT_LEVEL_INPUT_DP_H,
    BATT_LEVEL_INPUT_CCB_H,

    /* Telink SPP: handle order intentionally kept compatible with previous BMS. */
    SPP_PS_H,
    SPP_SERVER_TO_CLIENT_CD_H,
    SPP_SERVER_TO_CLIENT_DP_H,
    SPP_SERVER_TO_CLIENT_DESC_H,
    SPP_CLIENT_TO_SERVER_CD_H,
    SPP_CLIENT_TO_SERVER_DP_H,
    SPP_SERVER_TO_CLIENT_CCB_H,
    SPP_CLIENT_TO_SERVER_DESC_H,

#if (BLE_OTA_SERVER_ENABLE)
    /* Telink OTA */
    OTA_PS_H,
    OTA_CMD_OUT_CD_H,
    OTA_CMD_OUT_DP_H,
    OTA_CMD_INPUT_CCB_H,
    OTA_CMD_OUT_DESC_H,
#endif

    ATT_END_H,
} ATT_HANDLE;

/* GAP device name storage is updated together with the scan-response name. */
extern u8 my_devName[BTNAME_TOTAL_MAX_LEN];

void my_att_init(void);

#endif /* APP_ATT_H_ */
