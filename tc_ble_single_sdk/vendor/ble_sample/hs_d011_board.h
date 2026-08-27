#ifndef HS_D011_BOARD_H_
#define HS_D011_BOARD_H_

#include "tl_common.h"
#include "drivers.h"

#define BMS_BOARD_NAME                  "HS-D011-10S50A-V1"
#define BMS_CELL_COUNT                  10u

/* SH3673510 SPI: schematic U3/U5 mapping.
 * PB7=MOSI(SDO from Telink), PB6=MISO(SDI to Telink), PD7=SCLK, PD2=CS-M.
 */
#define BMS_AFE_SPI_GROUP               SPI_GPIO_GROUP_B6B7D2D7
#define BMS_AFE_CS_PIN                  GPIO_PD2
#define BMS_AFE_RESET_PIN               GPIO_PC1
#define BMS_AFE_ALARM_PIN               GPIO_PC0

/* Isolated RS485 (CA-IS2092A). */
#define BMS_RS485_TX_PIN                UART_TX_PC2
#define BMS_RS485_RX_PIN                UART_RX_PC3
#define BMS_RS485_EN_PIN                GPIO_PA1
#define BMS_RS485_BAUD                  115200u
#define BMS_MODBUS_SLAVE_ADDR           0x01u

/* Other schematic nets kept in one board layer. */
#define BMS_DI1_PIN                     GPIO_PA0
#define BMS_INT_WAKE_MCU_PIN            GPIO_PB1
#define BMS_HEAT_CHG_PIN                GPIO_PB4
#define BMS_HEAT_RF_EN_PIN              GPIO_PB5
#define BMS_LED_PIN                     GPIO_PC4
#define BMS_CMNT_EN_PIN                 GPIO_PD4
#define BMS_CMNT_WAKE_PIN               GPIO_PD3

/* Board routes TS1/TS2 and TS4-MOS; TS3 is NC. */
#define BMS_AFE_TS1_ENABLE              1u
#define BMS_AFE_TS2_ENABLE              1u
#define BMS_AFE_TS3_ENABLE              0u
#define BMS_AFE_TS4_ENABLE              1u

/*
 * Schematic has RS1..RS8, each 2 mOhm, connected in parallel in the main
 * current path. Effective sense resistance is therefore 2mOhm / 8 = 0.25mOhm.
 * Keep this overrideable for BOM variants.
 */
#ifndef BMS_RSENSE_UOHM
#define BMS_RSENSE_UOHM                 250u
#endif

#define BMS_AFE_POLL_PERIOD_US          100000u

#endif /* HS_D011_BOARD_H_ */
