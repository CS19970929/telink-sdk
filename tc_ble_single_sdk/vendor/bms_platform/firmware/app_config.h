#ifndef BMS_FIRMWARE_APP_CONFIG_H
#define BMS_FIRMWARE_APP_CONFIG_H

/* This is a compile profile, not a board pin assignment. */
#define BMS_BOARD_PROFILE_COMPILE_ONLY       1

#define BLE_APP_PM_ENABLE                    0
#define PM_DEEPSLEEP_RETENTION_ENABLE        0
#define BLE_APP_SECURITY_ENABLE              1
#define BLE_OTA_SERVER_ENABLE                1
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
