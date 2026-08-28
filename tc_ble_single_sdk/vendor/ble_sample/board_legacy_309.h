#ifndef BOARD_LEGACY_309_H_
#define BOARD_LEGACY_309_H_

/* Existing TLSR8251 + SH367309 validation board, reconstructed from the
 * codex-new-new-master-no-ide-toolchain reference project (D3PRO profile).
 */
#define BMS_BOARD_NAME                  "TLSR8251-SH367309-LEGACY"
#define BMS_CELL_COUNT                  10u

/* Reference firmware uses Telink I2C group C0/C1, AFE address 0x34 and 100 kHz. */
#define BMS_SH309_I2C_GROUP             I2C_GPIO_GROUP_C0C1
#define BMS_SH309_I2C_ADDR              0x34u
#define BMS_SH309_I2C_HZ                100000u
#define BMS_SH309_I2C_RETRY_COUNT       3u
#define BMS_SH309_BOOT_SETTLE_MS        100u
#define BMS_SH309_READY_TIMEOUT_MS      1000u

/* D3PRO reference: two 2 mOhm shunts in parallel => 1 mOhm effective. */
#ifndef BMS_RSENSE_UOHM
#define BMS_RSENSE_UOHM                 1000u
#endif

/* The reference data path exposes two battery NTC channels to the application.
 * TEMP3 exists in SH367309 RAM and is decoded for diagnostics, but is not part
 * of the active protection temperature mask until the board population is
 * confirmed.
 */
#define BMS_AFE_TS1_ENABLE              1u
#define BMS_AFE_TS2_ENABLE              1u
#define BMS_AFE_TS3_ENABLE              0u
#define BMS_AFE_TS4_ENABLE              0u

/* 10S D3PRO is identity-wired in the legacy SeriesSelect_AFE1 table. Keep the
 * map board-owned so another SH367309 PCB can remap channels without touching
 * the chip driver.
 */
#define BMS_SH309_CELL_MAP_INIT         {0u,1u,2u,3u,4u,5u,6u,7u,8u,9u}

/* Keep the already-proven AFE parameter image untouched, but now take over the
 * runtime CHG/DSG MOS bits after a valid AFE sample has passed common software
 * protection. The reference D3PRO board additionally uses PB6 as AFE_CTL and
 * PA1 as MCC_C; both start LOW and are only asserted by a successful MOS write.
 */
#ifndef BMS_SH309_RESET_ON_INIT
#define BMS_SH309_RESET_ON_INIT         0u
#endif
#ifndef BMS_SH309_MOS_CONTROL_ENABLE
#define BMS_SH309_MOS_CONTROL_ENABLE    1u
#endif
#define BMS_SH309_AFE_CTL_PIN           GPIO_PB6
#define BMS_SH309_MCC_C_PIN             GPIO_PA1
#define BMS_SH309_GATE_ACTIVE_LEVEL     1u
#define BMS_SH309_MCC_C_ACTIVE_LEVEL    1u

/* The proven legacy project uses a direct 115200 8N1 UART on PC2/PC3. PA1 is
 * MCC_C on this PCB, so there is deliberately no RS485 direction pin here.
 * Keep GPIO identities beside the UART mux identities so PC3 can temporarily
 * become a low-level wake pad after the serial link has been idle for 3 s.
 */
#define BMS_SERIAL_ENABLE               1u
#define BMS_SERIAL_TX_PIN               UART_TX_PC2
#define BMS_SERIAL_RX_PIN               UART_RX_PC3
#define BMS_SERIAL_TX_GPIO              GPIO_PC2
#define BMS_SERIAL_RX_GPIO              GPIO_PC3
#define BMS_SERIAL_DE_ENABLE            0u
#define BMS_SERIAL_BAUD                 115200u
#define BMS_MODBUS_SLAVE_ADDR           0x01u

/* Compatibility name consumed by the existing scheduler/project code. */
#define BMS_RS485_ENABLE                BMS_SERIAL_ENABLE
#define BMS_RS485_TX_PIN                BMS_SERIAL_TX_PIN
#define BMS_RS485_RX_PIN                BMS_SERIAL_RX_PIN
#define BMS_RS485_BAUD                  BMS_SERIAL_BAUD

#define BMS_AFE_POLL_PERIOD_US          100000u

#define BMS_PARAM_FLASH_SLOT_A          0x72000u
#define BMS_PARAM_FLASH_SLOT_B          0x73000u
#define BMS_PARAM_FLASH_REQUIRED_SIZE   FLASH_SIZE_512K

#define BMS_PROTECT_OPPOSITE_REOPEN_ENABLE 1u

/* sh3673510.c remains in the deterministic source-order file for the other
 * board profile. These compile-only definitions let that dormant translation
 * unit compile under the 309 profile; BMS_AFE_MODEL dispatch means none of its
 * functions are referenced or executed, and --gc-sections removes them.
 */
#define BMS_AFE_SPI_GROUP               SPI_GPIO_GROUP_B6B7D2D7
#define BMS_AFE_CS_PIN                  GPIO_PD2
#define BMS_AFE_RESET_PIN               GPIO_PC1
#define BMS_AFE_ALARM_PIN               GPIO_PC0

#endif /* BOARD_LEGACY_309_H_ */
