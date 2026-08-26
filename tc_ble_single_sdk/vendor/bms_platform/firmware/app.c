#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "bms_firmware.h"
#include "bms_gatt.h"
#include "bms_lab_simulator.h"

#define BMS_RX_FIFO_SIZE                 (64u)
#define BMS_RX_FIFO_NUM                  (8u)
#define BMS_TX_FIFO_SIZE                 (40u)
#define BMS_TX_FIFO_NUM                  (16u)
#define BMS_ADV_INTERVAL_MIN             ADV_INTERVAL_100MS
#define BMS_ADV_INTERVAL_MAX             ADV_INTERVAL_105MS
#define BMS_ADV_CHANNEL                  (BLT_ENABLE_ADV_37 | BLT_ENABLE_ADV_38 | BLT_ENABLE_ADV_39)
#define BMS_PC2_TOGGLE_INTERVAL_US       (1000000u)
#define BMS_ADV_DATA_MAX_LENGTH           (31u)
#define BMS_LAB_CONFIG_SLOT0_ADDRESS      (0x72000u)
#define BMS_LAB_CONFIG_SLOT1_ADDRESS      (0x73000u)
#define BMS_FLASH_SECTOR_SIZE              (0x1000u)

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
static u32 g_pc2_toggle_tick;
static u8 g_pc2_level;
static u8 g_advertising_data[BMS_ADV_DATA_MAX_LENGTH];
static u8 g_advertising_data_length;
/* Put the stable service identifier in scan response; the local name stays configurable. */
static const u8 g_scan_response[] = {
    0x11u, 0x07u,
    0x57u, 0x94u, 0x68u, 0x20u, 0x8eu, 0x5eu, 0x44u, 0x91u,
    0x92u, 0x46u, 0x0du, 0xa0u, 0x01u, 0x00u, 0xa5u, 0xb1u
};

#if (BMS_LAB_CONFIG_FLASH_ENABLE)
/* Keep the laboratory control path RAM-only until board Flash is approved. */
static uint8_t g_bms_lab_config_slots[BMS_CONFIG_REQUIRED_SLOTS][BMS_CONFIG_SLOT_SIZE];

static int bms_app_config_slot_index(uint32_t address)
{
    if (address == BMS_LAB_CONFIG_SLOT0_ADDRESS) {
        return 0;
    }
    if (address == BMS_LAB_CONFIG_SLOT1_ADDRESS) {
        return 1;
    }
    return -1;
}

static BmsStatus bms_app_config_flash_read(void *context, uint32_t address,
                                           uint8_t *data, uint16_t length)
{
    int slot;
    uint16_t index;
    (void)context;
    slot = bms_app_config_slot_index(address);
    if ((data == 0) || (slot < 0) || (length > BMS_CONFIG_SLOT_SIZE)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < length; ++index) {
        data[index] = g_bms_lab_config_slots[slot][index];
    }
    return BMS_STATUS_OK;
}

static BmsStatus bms_app_config_flash_erase(void *context, uint32_t address, uint16_t length)
{
    int slot;
    uint16_t index;
    (void)context;
    slot = bms_app_config_slot_index(address);
    if ((slot < 0) || (length != BMS_FLASH_SECTOR_SIZE)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < BMS_CONFIG_SLOT_SIZE; ++index) {
        g_bms_lab_config_slots[slot][index] = 0xffu;
    }
    return BMS_STATUS_OK;
}

static BmsStatus bms_app_config_flash_write(void *context, uint32_t address,
                                            const uint8_t *data, uint16_t length)
{
    int slot;
    uint16_t index;
    (void)context;
    slot = bms_app_config_slot_index(address);
    if ((data == 0) || (slot < 0) || (length > BMS_CONFIG_SLOT_SIZE)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < length; ++index) {
        g_bms_lab_config_slots[slot][index] = data[index];
    }
    return BMS_STATUS_OK;
}

static const BmsConfigStore g_bms_lab_config_store = {
    bms_app_config_flash_read,
    bms_app_config_flash_erase,
    bms_app_config_flash_write,
    0,
    {BMS_LAB_CONFIG_SLOT0_ADDRESS, BMS_LAB_CONFIG_SLOT1_ADDRESS},
    BMS_FLASH_SECTOR_SIZE
};

static BmsStatus bms_app_init_persistent_config(void)
{
    uint8_t slot;
    uint16_t index;
    for (slot = 0u; slot < BMS_CONFIG_REQUIRED_SLOTS; ++slot) {
        for (index = 0u; index < BMS_CONFIG_SLOT_SIZE; ++index) {
            g_bms_lab_config_slots[slot][index] = 0xffu;
        }
    }
    return bms_firmware_set_config_store(&g_bms_lab_config_store);
}
#endif

static BmsStatus bms_app_set_advertising_name(const uint8_t *name, uint8_t length)
{
    uint8_t index;

    if ((name == 0) || (length == 0u) || (length > BMS_BLE_NAME_MAX_BYTES)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    g_advertising_data[0] = (uint8_t)(length + 1u);
    g_advertising_data[1] = 0x09u;
    for (index = 0u; index < length; ++index) {
        g_advertising_data[index + 2u] = name[index];
    }
    g_advertising_data[length + 2u] = 0x02u;
    g_advertising_data[length + 3u] = 0x01u;
    g_advertising_data[length + 4u] = 0x06u;
    g_advertising_data_length = (uint8_t)(length + 5u);
    return (bls_ll_setAdvData(g_advertising_data, g_advertising_data_length) == BLE_SUCCESS) ?
           BMS_STATUS_OK : BMS_STATUS_IO_ERROR;
}

static BmsStatus bms_app_set_ble_name(void *context, const uint8_t *name, uint8_t length)
{
    BmsStatus status;

    (void)context;
    status = bms_gatt_set_device_name(name, length);
    if (status != BMS_STATUS_OK) {
        return status;
    }
    return bms_app_set_advertising_name(name, length);
}

static void bms_app_init_pc2_heartbeat(void)
{
    gpio_set_func(GPIO_PC2, AS_GPIO);
    gpio_set_input_en(GPIO_PC2, 0u);
    gpio_write(GPIO_PC2, 0u);
    gpio_set_output_en(GPIO_PC2, 1u);
    g_pc2_level = 0u;
    g_pc2_toggle_tick = clock_time();
}

static void bms_app_process_pc2_heartbeat(void)
{
    if (clock_time_exceed(g_pc2_toggle_tick, BMS_PC2_TOGGLE_INTERVAL_US)) {
        g_pc2_toggle_tick = clock_time();
        g_pc2_level ^= 1u;
        gpio_write(GPIO_PC2, g_pc2_level);
    }
}

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
    bms_firmware_set_ble_name_setter(bms_app_set_ble_name, 0);
    bms_app_init_pc2_heartbeat();
#if (BMS_LAB_SIMULATOR_ENABLE)
    bms_lab_simulator_init();
#endif
    (void)bms_app_set_advertising_name((const uint8_t *)BMS_BLE_DEFAULT_NAME,
                                       sizeof(BMS_BLE_DEFAULT_NAME) - 1u);
#if (BMS_LAB_CONFIG_FLASH_ENABLE)
    (void)bms_app_init_persistent_config();
#endif
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
#if (BMS_LAB_SIMULATOR_ENABLE)
    bms_lab_simulator_process(clock_time() / CLOCK_SYS_CLOCK_1MS);
#endif
    bms_gatt_process();
    bms_firmware_process();
    bms_app_process_pc2_heartbeat();
}
