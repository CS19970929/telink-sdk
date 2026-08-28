#include "tl_common.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "app_att.h"
#include "modbus_rtu.h"
#include "btname_modbus.h"
#include <string.h>

/* UUIDs intentionally match the previous Telink BMS implementation. */
static const u8 TelinkSppServiceUUID[16] = WRAPPING_BRACES(TELINK_SPP_UUID_SERVICE);
static const u8 TelinkSppDataServer2ClientUUID[16] = WRAPPING_BRACES(TELINK_SPP_DATA_SERVER2CLIENT);
static const u8 TelinkSppDataClient2ServerUUID[16] = WRAPPING_BRACES(TELINK_SPP_DATA_CLIENT2SERVER);

static u8 SppNotifyCCC[2] = {0, 0};
static u8 SppWriteData[1] = {0};
static u8 SppNotifyData[1] = {0};
static const u8 TelinkSPPS2CDescriptor[] = "Telink SPP: Module->Phone";
static const u8 TelinkSPPC2SDescriptor[] = "Telink SPP: Phone->Module";

/* Previous BMS uses the historical Telink UUID names but reverses the data
 * direction at the ATT property level: phone writes S2C UUID, module notifies
 * C2S UUID. Keep this exactly for existing apps/tools.
 */
static const u8 TelinkSppWriteCharVal[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_WRITE,
    U16_LO(SPP_SERVER_TO_CLIENT_DP_H), U16_HI(SPP_SERVER_TO_CLIENT_DP_H),
    TELINK_SPP_DATA_SERVER2CLIENT
};
static const u8 TelinkSppNotifyCharVal[19] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(SPP_CLIENT_TO_SERVER_DP_H), U16_HI(SPP_CLIENT_TO_SERVER_DP_H),
    TELINK_SPP_DATA_CLIENT2SERVER
};

static const u16 clientCharacterCfgUUID = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 userdesc_UUID = GATT_UUID_CHAR_USER_DESC;
static const u16 serviceChangeUUID = GATT_UUID_SERVICE_CHANGE;
static const u16 my_primaryServiceUUID = GATT_UUID_PRIMARY_SERVICE;
static const u16 my_characterUUID = GATT_UUID_CHARACTER;
static const u16 my_devServiceUUID = SERVICE_UUID_DEVICE_INFORMATION;
static const u16 my_PnPUUID = CHARACTERISTIC_UUID_PNP_ID;
static const u16 my_devNameUUID = GATT_UUID_DEVICE_NAME;
static const u16 my_gapServiceUUID = SERVICE_UUID_GENERIC_ACCESS;
static const u16 my_appearanceUUID = GATT_UUID_APPEARANCE;
static const u16 my_periConnParamUUID = GATT_UUID_PERI_CONN_PARAM;
static const u16 my_appearance = GAP_APPEARE_UNKNOWN;
static const u16 my_gattServiceUUID = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const u16 my_periConnParameters[4] = {20, 40, 0, 1000};

_attribute_data_retention_ static u16 serviceChangeVal[2] = {0, 0};
_attribute_data_retention_ static u8 serviceChangeCCC[2] = {0, 0};

u8 my_devName[BTNAME_TOTAL_MAX_LEN] = "BT_d011_default";
static const u8 my_PnPtrs[] = {0x02, 0x8a, 0x24, 0x66, 0x82, 0x01, 0x00};

static const u16 my_batServiceUUID = SERVICE_UUID_BATTERY;
static const u16 my_batCharUUID = CHARACTERISTIC_UUID_BATTERY_LEVEL;
_attribute_data_retention_ static u8 batteryValueInCCC[2] = {0, 0};
_attribute_data_retention_ static u8 my_batVal[1] = {99};

#if (BLE_OTA_SERVER_ENABLE)
static const u8 my_OtaServiceUUID[16] = WRAPPING_BRACES(TELINK_OTA_UUID_SERVICE);
static const u8 my_OtaUUID[16] = WRAPPING_BRACES(TELINK_SPP_DATA_OTA);
_attribute_data_retention_ static u8 my_OtaData = 0x00;
_attribute_data_retention_ static u8 otaDataCCC[2] = {0, 0};
static const u8 my_OtaName[] = {'O', 'T', 'A'};
#endif

static const u8 my_devNameCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(GenericAccess_DeviceName_DP_H), U16_HI(GenericAccess_DeviceName_DP_H),
    U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)
};
static const u8 my_appearanceCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(GenericAccess_Appearance_DP_H), U16_HI(GenericAccess_Appearance_DP_H),
    U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)
};
static const u8 my_periConnParamCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(CONN_PARAM_DP_H), U16_HI(CONN_PARAM_DP_H),
    U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)
};
static const u8 my_serviceChangeCharVal[5] = {
    CHAR_PROP_INDICATE,
    U16_LO(GenericAttribute_ServiceChanged_DP_H), U16_HI(GenericAttribute_ServiceChanged_DP_H),
    U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)
};
static const u8 my_PnCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(DeviceInformation_pnpID_DP_H), U16_HI(DeviceInformation_pnpID_DP_H),
    U16_LO(CHARACTERISTIC_UUID_PNP_ID), U16_HI(CHARACTERISTIC_UUID_PNP_ID)
};
static const u8 my_batCharVal[5] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(BATT_LEVEL_INPUT_DP_H), U16_HI(BATT_LEVEL_INPUT_DP_H),
    U16_LO(CHARACTERISTIC_UUID_BATTERY_LEVEL), U16_HI(CHARACTERISTIC_UUID_BATTERY_LEVEL)
};

#if (BLE_OTA_SERVER_ENABLE)
static const u8 my_OtaCharVal[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY | CHAR_PROP_WRITE,
    U16_LO(OTA_CMD_OUT_DP_H), U16_HI(OTA_CMD_OUT_DP_H),
    TELINK_SPP_DATA_OTA
};
#endif

#define BMS_BLE_NOTIFY_PAYLOAD 20u
static u8 s_ble_rsp[512];

static ble_sts_t bms_notify_big_packet(u16 conn, u16 handle, const u8 *data, u16 len)
{
    u16 offset = 0;
    while (offset < len) {
        u8 chunk = (u8)(((len - offset) > BMS_BLE_NOTIFY_PAYLOAD) ?
                         BMS_BLE_NOTIFY_PAYLOAD : (len - offset));
        ble_sts_t rc = blc_gatt_pushHandleValueNotify(conn, handle, (u8 *)(data + offset), chunk);
        if (rc != BLE_SUCCESS) return rc;
        offset = (u16)(offset + chunk);
    }
    return BLE_SUCCESS;
}

static int bms_spp_write_cb(void *para)
{
    rf_packet_att_write_t *p = (rf_packet_att_write_t *)para;
    u16 len;
    u32 rsp_len = 0;

    if (!p || p->l2capLen < 3u) return 0;
    len = (u16)(p->l2capLen - 3u);
    if (len && modbus_on_frame((const u8 *)&p->value, len, s_ble_rsp, &rsp_len) && rsp_len) {
        (void)bms_notify_big_packet(BLS_CONN_HANDLE,
                                    SPP_CLIENT_TO_SERVER_DP_H,
                                    s_ble_rsp,
                                    (u16)rsp_len);
    }
    return 0;
}

static const attribute_t my_Attributes[] = {
    {ATT_END_H - 1, 0, 0, 0, 0, 0, 0, 0},

    /* GAP: 0x0001..0x0007 */
    {7, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&my_primaryServiceUUID, (u8 *)&my_gapServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_devNameCharVal), (u8 *)&my_characterUUID, (u8 *)my_devNameCharVal, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_devName), (u8 *)&my_devNameUUID, (u8 *)my_devName, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearanceCharVal), (u8 *)&my_characterUUID, (u8 *)my_appearanceCharVal, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearance), (u8 *)&my_appearanceUUID, (u8 *)&my_appearance, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_periConnParamCharVal), (u8 *)&my_characterUUID, (u8 *)my_periConnParamCharVal, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_periConnParameters), (u8 *)&my_periConnParamUUID, (u8 *)my_periConnParameters, 0, 0},

    /* GATT */
    {4, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&my_primaryServiceUUID, (u8 *)&my_gattServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_serviceChangeCharVal), (u8 *)&my_characterUUID, (u8 *)my_serviceChangeCharVal, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(serviceChangeVal), (u8 *)&serviceChangeUUID, (u8 *)serviceChangeVal, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(serviceChangeCCC), (u8 *)&clientCharacterCfgUUID, (u8 *)serviceChangeCCC, 0, 0},

    /* Device Information */
    {3, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&my_primaryServiceUUID, (u8 *)&my_devServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_PnCharVal), (u8 *)&my_characterUUID, (u8 *)my_PnCharVal, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_PnPtrs), (u8 *)&my_PnPUUID, (u8 *)my_PnPtrs, 0, 0},

    /* Battery */
    {4, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&my_primaryServiceUUID, (u8 *)&my_batServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_batCharVal), (u8 *)&my_characterUUID, (u8 *)my_batCharVal, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_batVal), (u8 *)&my_batCharUUID, (u8 *)my_batVal, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(batteryValueInCCC), (u8 *)&clientCharacterCfgUUID, (u8 *)batteryValueInCCC, 0, 0},

    /* Telink SPP: same handles/directions as existing BMS apps. */
    {8, ATT_PERMISSIONS_READ, 2, 16, (u8 *)&my_primaryServiceUUID, (u8 *)&TelinkSppServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(TelinkSppWriteCharVal), (u8 *)&my_characterUUID, (u8 *)TelinkSppWriteCharVal, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, sizeof(SppWriteData), (u8 *)&TelinkSppDataServer2ClientUUID, (u8 *)SppWriteData, (att_readwrite_callback_t)bms_spp_write_cb, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(TelinkSPPS2CDescriptor), (u8 *)&userdesc_UUID, (u8 *)TelinkSPPS2CDescriptor, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(TelinkSppNotifyCharVal), (u8 *)&my_characterUUID, (u8 *)TelinkSppNotifyCharVal, 0, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(SppNotifyData), (u8 *)&TelinkSppDataClient2ServerUUID, (u8 *)SppNotifyData, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(SppNotifyCCC), (u8 *)&clientCharacterCfgUUID, (u8 *)SppNotifyCCC, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(TelinkSPPC2SDescriptor), (u8 *)&userdesc_UUID, (u8 *)TelinkSPPC2SDescriptor, 0, 0},

#if (BLE_OTA_SERVER_ENABLE)
    /* Telink OTA */
    {5, ATT_PERMISSIONS_READ, 2, 16, (u8 *)&my_primaryServiceUUID, (u8 *)&my_OtaServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_OtaCharVal), (u8 *)&my_characterUUID, (u8 *)my_OtaCharVal, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, sizeof(my_OtaData), (u8 *)&my_OtaUUID, (u8 *)&my_OtaData, &otaWrite, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(otaDataCCC), (u8 *)&clientCharacterCfgUUID, (u8 *)otaDataCCC, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_OtaName), (u8 *)&userdesc_UUID, (u8 *)my_OtaName, 0, 0},
#endif
};

void my_att_init(void)
{
    bls_att_setAttributeTable((u8 *)my_Attributes);
}
