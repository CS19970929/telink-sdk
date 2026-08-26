#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "bms_firmware.h"
#include "bms_gatt.h"

#define BMS_GATT_NOTIFY_FRAGMENT_MAX       (MTU_SIZE_SETTING - 3u)

typedef enum {
    BMS_GATT_HANDLE_START = 0,
    BMS_GATT_GAP_SERVICE = 1,
    BMS_GATT_GAP_NAME_DECL,
    BMS_GATT_GAP_NAME_VALUE,
    BMS_GATT_SERVICE,
    BMS_GATT_RX_DECL,
    BMS_GATT_RX_VALUE,
    BMS_GATT_TX_DECL,
    BMS_GATT_TX_VALUE,
    BMS_GATT_TX_CCC,
    BMS_GATT_OTA_SERVICE,
    BMS_GATT_OTA_DECL,
    BMS_GATT_OTA_VALUE,
    BMS_GATT_OTA_CCC,
    BMS_GATT_END
} BmsGattHandle;

static const u16 g_primary_service_uuid = GATT_UUID_PRIMARY_SERVICE;
static const u16 g_characteristic_uuid = GATT_UUID_CHARACTER;
static const u16 g_device_name_uuid = GATT_UUID_DEVICE_NAME;
static const u16 g_gap_service_uuid = SERVICE_UUID_GENERIC_ACCESS;
static const u16 g_ccc_uuid = GATT_UUID_CLIENT_CHAR_CFG;
static const u8 g_bms_service_uuid[16] = {
    0x57u, 0x94u, 0x68u, 0x20u, 0x8eu, 0x5eu, 0x44u, 0x91u,
    0x92u, 0x46u, 0x0du, 0xa0u, 0x01u, 0x00u, 0xa5u, 0xb1u
};
static const u8 g_bms_rx_uuid[16] = {
    0x58u, 0x94u, 0x68u, 0x20u, 0x8eu, 0x5eu, 0x44u, 0x91u,
    0x92u, 0x46u, 0x0du, 0xa0u, 0x01u, 0x00u, 0xa5u, 0xb1u
};
static const u8 g_bms_tx_uuid[16] = {
    0x59u, 0x94u, 0x68u, 0x20u, 0x8eu, 0x5eu, 0x44u, 0x91u,
    0x92u, 0x46u, 0x0du, 0xa0u, 0x01u, 0x00u, 0xa5u, 0xb1u
};
static const u8 g_ota_service_uuid[16] = WRAPPING_BRACES(TELINK_OTA_UUID_SERVICE);
static const u8 g_ota_value_uuid[16] = WRAPPING_BRACES(TELINK_SPP_DATA_OTA);
static u8 g_device_name[BMS_BLE_NAME_MAX_BYTES] = BMS_BLE_DEFAULT_NAME;
static u8 g_device_name_length = sizeof(BMS_BLE_DEFAULT_NAME) - 1u;
/* The PC client sends one BMSLink frame in several 20-byte ATT writes. */
static u8 g_rx_value[BMSLINK_MAX_FRAME_SIZE];
static u8 g_tx_value[BMS_GATT_NOTIFY_FRAGMENT_MAX];
static u8 g_tx_ccc[2];
static u8 g_ota_value[1];
static u8 g_ota_ccc[2];
static u8 g_tx_buffer[BMSLINK_MAX_FRAME_SIZE];
static uint16_t g_tx_length;
static uint16_t g_tx_offset;

static const u8 g_name_declaration[] = {
    CHAR_PROP_READ,
    U16_LO(BMS_GATT_GAP_NAME_VALUE), U16_HI(BMS_GATT_GAP_NAME_VALUE),
    U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)
};
static const u8 g_rx_declaration[] = {
    CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP,
    U16_LO(BMS_GATT_RX_VALUE), U16_HI(BMS_GATT_RX_VALUE),
    0x58u, 0x94u, 0x68u, 0x20u, 0x8eu, 0x5eu, 0x44u, 0x91u,
    0x92u, 0x46u, 0x0du, 0xa0u, 0x01u, 0x00u, 0xa5u, 0xb1u
};
static const u8 g_tx_declaration[] = {
    CHAR_PROP_NOTIFY,
    U16_LO(BMS_GATT_TX_VALUE), U16_HI(BMS_GATT_TX_VALUE),
    0x59u, 0x94u, 0x68u, 0x20u, 0x8eu, 0x5eu, 0x44u, 0x91u,
    0x92u, 0x46u, 0x0du, 0xa0u, 0x01u, 0x00u, 0xa5u, 0xb1u
};
static const u8 g_ota_declaration[] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY,
    U16_LO(BMS_GATT_OTA_VALUE), U16_HI(BMS_GATT_OTA_VALUE),
    TELINK_SPP_DATA_OTA
};

static int bms_gatt_receive(void *parameter)
{
    rf_packet_att_write_t *write = (rf_packet_att_write_t *)parameter;
    uint16_t length;

    if (write->l2capLen < 3u) {
        return 0;
    }
    length = (uint16_t)(write->l2capLen - 3u);
    (void)bms_firmware_receive(&write->value, length);
    return 0;
}

static int bms_gatt_ota_receive(void *parameter)
{
#if (BLE_OTA_SERVER_ENABLE && BMS_OTA_LAYOUT_APPROVED)
    return otaWrite(parameter);
#else
    /* Do not reach the SDK Flash writer before the board layout is approved. */
    (void)parameter;
    return 0;
#endif
}

static attribute_t g_attributes[] = {
    {BMS_GATT_END - 1u, 0u, 0u, 0u, 0, 0, 0, 0},
    {3u, ATT_PERMISSIONS_READ, 2u, 2u, (u8 *)&g_primary_service_uuid,
     (u8 *)&g_gap_service_uuid, 0, 0},
    {0u, ATT_PERMISSIONS_READ, 2u, sizeof(g_name_declaration),
     (u8 *)&g_characteristic_uuid, (u8 *)g_name_declaration, 0, 0},
    {0u, ATT_PERMISSIONS_READ, 2u, sizeof(BMS_BLE_DEFAULT_NAME) - 1u,
     (u8 *)&g_device_name_uuid, g_device_name, 0, 0},
    {6u, ATT_PERMISSIONS_READ, 2u, 16u, (u8 *)&g_primary_service_uuid,
     (u8 *)g_bms_service_uuid, 0, 0},
    {0u, ATT_PERMISSIONS_READ, 2u, sizeof(g_rx_declaration),
     (u8 *)&g_characteristic_uuid, (u8 *)g_rx_declaration, 0, 0},
    {0u, ATT_PERMISSIONS_WRITE, 16u, sizeof(g_rx_value), (u8 *)g_bms_rx_uuid,
     g_rx_value, bms_gatt_receive, 0},
    {0u, ATT_PERMISSIONS_READ, 2u, sizeof(g_tx_declaration),
     (u8 *)&g_characteristic_uuid, (u8 *)g_tx_declaration, 0, 0},
    {0u, ATT_PERMISSIONS_READ, 16u, sizeof(g_tx_value), (u8 *)g_bms_tx_uuid,
     g_tx_value, 0, 0},
    {0u, ATT_PERMISSIONS_RDWR, 2u, sizeof(g_tx_ccc), (u8 *)&g_ccc_uuid,
     g_tx_ccc, 0, 0},
    {4u, ATT_PERMISSIONS_READ, 2u, 16u, (u8 *)&g_primary_service_uuid,
     (u8 *)g_ota_service_uuid, 0, 0},
    {0u, ATT_PERMISSIONS_READ, 2u, sizeof(g_ota_declaration),
     (u8 *)&g_characteristic_uuid, (u8 *)g_ota_declaration, 0, 0},
    {0u, ATT_PERMISSIONS_RDWR, 16u, sizeof(g_ota_value), (u8 *)g_ota_value_uuid,
     g_ota_value, bms_gatt_ota_receive, 0},
    {0u, ATT_PERMISSIONS_RDWR, 2u, sizeof(g_ota_ccc), (u8 *)&g_ccc_uuid,
     g_ota_ccc, 0, 0}
};

void bms_gatt_init(void)
{
    g_tx_length = 0u;
    g_tx_offset = 0u;
    bls_att_setAttributeTable((u8 *)g_attributes);
    bls_att_setDeviceName(g_device_name, g_device_name_length);
}

BmsStatus bms_gatt_set_device_name(const uint8_t *name, uint8_t length)
{
    uint8_t index;

    if ((name == 0) || (length == 0u) || (length > BMS_BLE_NAME_MAX_BYTES)) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < length; ++index) {
        g_device_name[index] = name[index];
    }
    g_device_name_length = length;
    g_attributes[BMS_GATT_GAP_NAME_VALUE].attrLen = length;
    if (bls_att_setDeviceName(g_device_name, g_device_name_length) != BLE_SUCCESS) {
        return BMS_STATUS_IO_ERROR;
    }
    return BMS_STATUS_OK;
}

BmsStatus bms_gatt_transmit(void *context, const uint8_t *data, uint16_t length)
{
    uint16_t index;

    (void)context;
    if ((data == 0) || (length == 0u) || (length > sizeof(g_tx_buffer))) {
        return BMS_STATUS_INVALID_ARGUMENT;
    }
    if (g_tx_length != 0u) {
        return BMS_STATUS_NOT_READY;
    }
    for (index = 0u; index < length; ++index) {
        g_tx_buffer[index] = data[index];
    }
    g_tx_length = length;
    g_tx_offset = 0u;
    return BMS_STATUS_OK;
}

void bms_gatt_process(void)
{
    uint16_t remaining;
    uint16_t fragment;

    if (g_tx_offset >= g_tx_length) {
        g_tx_length = 0u;
        g_tx_offset = 0u;
        return;
    }
    remaining = (uint16_t)(g_tx_length - g_tx_offset);
    fragment = (remaining > BMS_GATT_NOTIFY_FRAGMENT_MAX) ?
               BMS_GATT_NOTIFY_FRAGMENT_MAX : remaining;
    if (bls_att_pushNotifyData(BMS_GATT_TX_VALUE,
                               &g_tx_buffer[g_tx_offset], fragment) == BLE_SUCCESS) {
        g_tx_offset = (uint16_t)(g_tx_offset + fragment);
    }
}

void bms_gatt_ota_started(void)
{
    (void)blc_ota_setOtaProcessTimeout(BMS_OTA_PROCESS_TIMEOUT_SECONDS);
}

void bms_gatt_ota_finished(int result)
{
    (void)result;
}
