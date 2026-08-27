/********************************************************************************************************
 * @file    app_config.h
 * @brief   HS-D011 BMS application configuration based on the established Telink BMS project.
 *******************************************************************************************************/
#pragma once

///////////////////////// Feature Configuration /////////////////////////////////////////////////////////
#define BLE_APP_PM_ENABLE                              1
#define PM_DEEPSLEEP_RETENTION_ENABLE                  0
/*
 * The Telink sample uses TEST_CONN_CURRENT_ENABLE to suppress its generic
 * 60-second DEEPSLEEP path. HS-D011 wake polarity/policy belongs to the BMS
 * power manager and must not be guessed here. BLE suspend remains enabled.
 */
#define TEST_CONN_CURRENT_ENABLE                       1
#define BLE_APP_SECURITY_ENABLE                        0
#define BLE_OTA_SERVER_ENABLE                          1
#define APP_FLASH_PROTECTION_ENABLE                    1
#define APP_BATT_CHECK_ENABLE                          0

///////////////////////// DEBUG Configuration ///////////////////////////////////////////////////////////
/* PC2/PC3 are RS485 and PB1 is INT-WK-MCU on HS-D011; UART debug must stay disabled. */
#define DEBUG_GPIO_ENABLE                              0
#define UART_PRINT_DEBUG_ENABLE                        0
#define APP_LOG_EN                                     0
#define APP_SMP_LOG_EN                                 0
#define APP_KEY_LOG_EN                                 0
#define APP_CONTR_EVENT_LOG_EN                         0
#define APP_HOST_EVENT_LOG_EN                          0
#define APP_OTA_LOG_EN                                 0
#define APP_FLASH_INIT_LOG_EN                          0
#define APP_FLASH_PROT_LOG_EN                          0
#define APP_BATT_CHECK_LOG_EN                          0

///////////////////////// OTA stability /////////////////////////////////////////////////////////////////
#define APP_OTA_PROCESS_TIMEOUT_S                      180
#define APP_OTA_DATA_PACKET_TIMEOUT_S                  15

///////////////////////// Sample Board Select Configuration /////////////////////////////////////////////
#if (__PROJECT_8258_BLE_SAMPLE__)
    #define BOARD_SELECT                               BOARD_825X_EVK_C1T139A30
#elif (__PROJECT_8278_BLE_SAMPLE__)
    #define BOARD_SELECT                               BOARD_827X_EVK_C1T197A30
#elif (__PROJECT_TC321X_BLE_SAMPLE__)
    #define BOARD_SELECT                               BOARD_TC321X_EVK_C1T357A20
#endif

///////////////////////// UI Configuration //////////////////////////////////////////////////////////////
#define UI_KEYBOARD_ENABLE                            0
#define UI_LED_ENABLE                                 0
#define UI_BUTTON_ENABLE                              0

///////////////////////// Deep-save flags ///////////////////////////////////////////////////////////////
#if (__PROJECT_TC321X_BLE_SAMPLE__)
    #define USED_DEEP_ANA_REG                          PM_ANA_REG_WD_CLR_BUF1
#else
    #define USED_DEEP_ANA_REG                          DEEP_ANA_REG0
#endif
#define LOW_BATT_FLG                                   BIT(0)
#define CONN_DEEP_FLG                                  BIT(4)

///////////////////////// System Clock //////////////////////////////////////////////////////////////////
#define CLOCK_SYS_CLOCK_HZ                             16000000

///////////////////////// Watchdog //////////////////////////////////////////////////////////////////////
#define MODULE_WATCHDOG_ENABLE                        1
#define WATCHDOG_INIT_TIMEOUT                         2000

#if TEST_CONN_CURRENT_ENABLE
    #if DEBUG_GPIO_ENABLE || UART_PRINT_DEBUG_ENABLE || UI_KEYBOARD_ENABLE || UI_LED_ENABLE || UI_BUTTON_ENABLE
        #error "If testing current, debug/UI definitions must be disabled"
    #endif
#endif

#include "vendor/common/default_config.h"
