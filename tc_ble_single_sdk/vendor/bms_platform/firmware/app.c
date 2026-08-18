#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "bms_firmware.h"
#include "bms_gatt.h"

#define BMS_RX_FIFO_SIZE                 (64u)
#define BMS_RX_FIFO_NUM                  (8u)
#define BMS_TX_FIFO_SIZE                 (40u)
#define BMS_TX_FIFO_NUM                  (16u)
#define BMS_ADV_INTERVAL_MIN             ADV_INTERVAL_100MS
#define BMS_ADV_INTERVAL_MAX             ADV_INTERVAL_105MS
#define BMS_ADV_CHANNEL                  (BLT_ENABLE_ADV_37 | BLT_ENABLE_ADV_38 | BLT_ENABLE_ADV_39)

_attribute_data_retention_ u8 blt_rxfifo_b[BMS_RX_FIFO_SIZE * BMS_RX_FIFO_NUM];
_attribute_data_retention_ my_fifo_t blt_rxfifo = {
    BMS_RX_FIFO_SIZE, BMS_RX_FIFO_NUM, 0u, 0u, blt_rxfifo_b
};
_attribute_data_retention_ u8 blt_txfifo_b[BMS_TX_FIFO_SIZE * BMS_TX_FIFO_NUM];
_attribute_data_retention_ my_fifo_t blt_txfifo = {
    BMS_TX_FIFO_SIZE, BMS_TX_FIFO_NUM, 0u, 0u, blt_txfifo_b
};

static own_addr_type_t g_own_address_type = OWN_ADDRESS_PUBLIC;
static uint8_t g_bms_write_session_authorized;
static const u8 g_advertising_data[] = {
    0x0bu, 0x09u, 'T', 'e', 'l', 'i', 'n', 'k', ' ', 'B', 'M', 'S',
    0x02u, 0x01u, 0x06u
};
static const u8 g_scan_response[] = {
    0x08u, 0x09u, 'B', 'M', 'S', 'L', 'i', 'n', 'k'
};

static void bms_app_on_connect(u8 event, u8 *parameters, int length)
{
    (void)event;
    (void)parameters;
    (void)length;
    g_bms_write_session_authorized = 0u;
}

static void bms_app_on_terminate(u8 event, u8 *parameters, int length)
{
    (void)event;
    (void)parameters;
    (void)length;
    g_bms_write_session_authorized = 0u;
}

static uint8_t bms_app_write_is_authorized(void *context)
{
    (void)context;
    return g_bms_write_session_authorized;
}

static int bms_app_host_event(u32 host_event, u8 *parameters, int length)
{
    (void)parameters;
    (void)length;
    switch ((u8)host_event) {
    case GAP_EVT_SMP_CONN_ENCRYPTION_DONE:
        g_bms_write_session_authorized = 1u;
        break;
    case GAP_EVT_SMP_PAIRING_FAIL:
        g_bms_write_session_authorized = 0u;
        break;
    default:
        break;
    }
    return 0;
}

void user_init_normal(void)
{
    u8 mac_public[6];
    u8 mac_random_static[6];
    u8 advertising_status;

    random_generator_init();
    blc_readFlashSize_autoConfigCustomFlashSector();
    blc_app_loadCustomizedParameters_normal();
    blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);

    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(mac_public);
    blc_ll_initAdvertising_module(mac_public);
    blc_ll_initConnection_module();
    blc_ll_initSlaveRole_module();

    blc_gap_peripheral_init();
    blc_l2cap_register_handler(blc_l2cap_packet_receive);
    bms_gatt_init();
    blc_att_setRxMtuSize(MTU_SIZE_SETTING);

#if (BLE_APP_SECURITY_ENABLE)
    /* Follow the SDK BLE peripheral SMP initialization order. */
    bls_smp_configPairingSecurityInfoStorageAddr(flash_sector_smp_storage);
    blc_smp_setSecurityLevel(Unauthenticated_Pairing_with_Encryption);
    blc_smp_setBondingMode(Bondable_Mode);
    blc_smp_setIoCapability(IO_CAPABILITY_NO_IN_NO_OUT);
    blc_smp_peripheral_init();
    blc_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000u);
    blc_gap_registerHostEventHandler(bms_app_host_event);
    blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_BEGIN |
                         GAP_EVT_MASK_SMP_PAIRING_SUCCESS |
                         GAP_EVT_MASK_SMP_PAIRING_FAIL |
                         GAP_EVT_MASK_SMP_CONN_ENCRYPTION_DONE);
#else
    blc_smp_setSecurityLevel(No_Security);
#endif

#if (BLE_OTA_SERVER_ENABLE && BMS_OTA_LAYOUT_APPROVED)
    blc_ota_initOtaServer_module();
    (void)blc_ota_setOtaProcessTimeout(BMS_OTA_PROCESS_TIMEOUT_SECONDS);
    (void)blc_ota_setOtaDataPacketTimeout(20u);
    bls_ota_registerStartCmdCb(bms_gatt_ota_started);
    bls_ota_registerResultIndicateCb(bms_gatt_ota_finished);
#endif

    bms_firmware_init(bms_gatt_transmit, 0);
    bms_firmware_set_write_authorizer(bms_app_write_is_authorized, 0);
    bls_ll_setAdvData((u8 *)g_advertising_data, sizeof(g_advertising_data));
    bls_ll_setScanRspData((u8 *)g_scan_response, sizeof(g_scan_response));
    advertising_status = bls_ll_setAdvParam(BMS_ADV_INTERVAL_MIN, BMS_ADV_INTERVAL_MAX,
                                             ADV_TYPE_CONNECTABLE_UNDIRECTED,
                                             g_own_address_type, 0, 0,
                                             BMS_ADV_CHANNEL, ADV_FP_NONE);
    if (advertising_status == BLE_SUCCESS) {
        bls_ll_setAdvEnable(BLC_ADV_ENABLE);
    }
    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, bms_app_on_connect);
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, bms_app_on_terminate);
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    blc_app_checkControllerHostInitialization();
}

void user_init_deepRetn(void)
{
    user_init_normal();
}

void main_loop(void)
{
    blt_sdk_main_loop();
    bms_gatt_process();
}
