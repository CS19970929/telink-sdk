#ifndef BMS_FIRMWARE_APP_CONFIG_H
#define BMS_FIRMWARE_APP_CONFIG_H

/* This is a compile profile, not a board pin assignment. */
#define BMS_BOARD_PROFILE_COMPILE_ONLY       1

/* The complete local name shares the 31-byte legacy advertising packet. */
// #define BMS_BLE_DEFAULT_NAME                  "Telink BMS"
#define BMS_BLE_DEFAULT_NAME                  "BT_Telink BMS"
#define BMS_BLE_NAME_MAX_BYTES                26u

/* The proof OTA target changes only PATCH to make a reboot observable. */
#ifndef BMS_FIRMWARE_VERSION_MAJOR
#define BMS_FIRMWARE_VERSION_MAJOR            0u
#endif
#ifndef BMS_FIRMWARE_VERSION_MINOR
#define BMS_FIRMWARE_VERSION_MINOR            2u
#endif
#ifndef BMS_FIRMWARE_VERSION_PATCH
#define BMS_FIRMWARE_VERSION_PATCH            0u
#endif
#if ((BMS_FIRMWARE_VERSION_MAJOR > 255u) || \
     (BMS_FIRMWARE_VERSION_MINOR > 255u) || \
     (BMS_FIRMWARE_VERSION_PATCH > 255u))
#error "BMS firmware version components must fit in one byte"
#endif

/* The simulator remains opt-in; OTA is enabled for every firmware profile. */
#ifndef BMS_LAB_SIMULATOR_ENABLE
#define BMS_LAB_SIMULATOR_ENABLE             0
#endif
#ifndef BMS_LAB_OTA_ENABLE
#define BMS_LAB_OTA_ENABLE                   0
#endif

#if (BMS_LAB_OTA_ENABLE && !BMS_LAB_SIMULATOR_ENABLE)
#error "The OTA laboratory image must also enable the laboratory simulator"
#endif

/* Only the official 512 KiB laboratory board receives reviewed config slots. */
#if (BMS_LAB_SIMULATOR_ENABLE)
#define BMS_LAB_CONFIG_FLASH_ENABLE          1
#else
#define BMS_LAB_CONFIG_FLASH_ENABLE          0
#endif

#define BLE_APP_PM_ENABLE                    0
#define PM_DEEPSLEEP_RETENTION_ENABLE        0
/* SMP bonding + encryption gate BMSLink write/CONTROL commands. */
#define BLE_APP_SECURITY_ENABLE              1
#define BLE_OTA_SERVER_ENABLE                1
#define BMS_CONFIG_PERSISTENT_ENABLE         0
/* OTA is part of every firmware profile and uses the reviewed SDK layout. */
#define BMS_OTA_PROCESS_TIMEOUT_SECONDS       180
/* SDK reference layout: 124 KiB image in the 0x20000 secondary slot. */
#define BMS_OTA_LAYOUT_APPROVED               1
#define BMS_OTA_FIRMWARE_SIZE_KB              124
#define BMS_OTA_BOOT_ADDRESS                  0x20000u
#define APP_FLASH_PROTECTION_ENABLE          0
#define APP_BATT_CHECK_ENABLE                0
#define UART_PRINT_DEBUG_ENABLE              0
#define DEBUG_GPIO_ENABLE                    0
#define MODULE_WATCHDOG_ENABLE               0
#define MTU_SIZE_SETTING                     64
#define CLOCK_SYS_CLOCK_HZ                   16000000

/*
 * The SDK has no dedicated TLSR8251 EVK profile. This value is only used to
 * make SDK common configuration compile; production pins must be defined in
 * board/ after the BMS schematic is confirmed.
 */
#define BOARD_SELECT                         BOARD_825X_EVK_C1T139A30

#endif /* BMS_FIRMWARE_APP_CONFIG_H */
