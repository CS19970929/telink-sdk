#ifndef BMS_FIRMWARE_APP_CONFIG_H
#define BMS_FIRMWARE_APP_CONFIG_H

/* This is a compile profile, not a board pin assignment. */
#define BMS_BOARD_PROFILE_COMPILE_ONLY       1

/* Only tools/bms.py lab targets set these values to 1. */
#ifndef BMS_LAB_SIMULATOR_ENABLE
#define BMS_LAB_SIMULATOR_ENABLE             0
#endif
#ifndef BMS_LAB_OTA_ENABLE
#define BMS_LAB_OTA_ENABLE                   0
#endif

#if (BMS_LAB_OTA_ENABLE && !BMS_LAB_SIMULATOR_ENABLE)
#error "The OTA laboratory image must also enable the laboratory simulator"
#endif

#define BLE_APP_PM_ENABLE                    0
#define PM_DEEPSLEEP_RETENTION_ENABLE        0
/* SMP bonding + encryption gate BMSLink write/CONTROL commands. */
#define BLE_APP_SECURITY_ENABLE              1
#define BLE_OTA_SERVER_ENABLE                1
/*
 * Keep OTA physically disabled until this product's Flash partition, image
 * size, boot address and recovery procedure have passed board review.
 * Setting this to 1 enables the SDK OTA server and its GATT write callback.
 */
#define BMS_OTA_PROCESS_TIMEOUT_SECONDS       180
/*
 * These must be set as one reviewed tuple before BMS_OTA_LAYOUT_APPROVED can
 * be enabled. 0 is intentionally invalid, so a partial configuration fails
 * at compile time instead of silently using the SDK's default image slot.
 */
#if (BMS_LAB_OTA_ENABLE)
/* SDK reference layout: 124 KiB image in the 0x20000 secondary slot. */
#define BMS_OTA_LAYOUT_APPROVED               1
#define BMS_OTA_FIRMWARE_SIZE_KB              124
#define BMS_OTA_BOOT_ADDRESS                  0x20000u
#else
#define BMS_OTA_LAYOUT_APPROVED               0
#define BMS_OTA_FIRMWARE_SIZE_KB              0
#define BMS_OTA_BOOT_ADDRESS                  0u
#endif
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
