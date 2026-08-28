#ifndef BMS_BOARD_H_
#define BMS_BOARD_H_

#include "tl_common.h"
#include "drivers.h"

/* Board and AFE are independent compile-time selections.  A board profile owns
 * only physical wiring/BOM facts; the AFE model owns register/protocol logic.
 * This keeps bms_project/modbus/protection independent of a specific AFE.
 */
#define BMS_BOARD_PROFILE_HS_D011       1u
#define BMS_BOARD_PROFILE_LEGACY_309    2u

#define BMS_AFE_MODEL_SIMULATED         0u
#define BMS_AFE_MODEL_SH367309          1u
#define BMS_AFE_MODEL_SH3673510         2u

/* Current bench target: the existing TLSR8251 + SH367309 board.  Override from
 * compiler flags or change this default when returning to HS-D011 bring-up.
 */
#ifndef BMS_BOARD_PROFILE
#define BMS_BOARD_PROFILE BMS_BOARD_PROFILE_LEGACY_309
#endif

#if (BMS_BOARD_PROFILE == BMS_BOARD_PROFILE_LEGACY_309)
#include "board_legacy_309.h"
#ifndef BMS_AFE_MODEL
#define BMS_AFE_MODEL BMS_AFE_MODEL_SH367309
#endif
#elif (BMS_BOARD_PROFILE == BMS_BOARD_PROFILE_HS_D011)
#include "board_hs_d011.h"
#ifndef BMS_AFE_MODEL
#define BMS_AFE_MODEL BMS_AFE_MODEL_SH3673510
#endif
#else
#error "Unsupported BMS_BOARD_PROFILE"
#endif

#if (BMS_AFE_MODEL != BMS_AFE_MODEL_SIMULATED) && \
    (BMS_AFE_MODEL != BMS_AFE_MODEL_SH367309) && \
    (BMS_AFE_MODEL != BMS_AFE_MODEL_SH3673510)
#error "Unsupported BMS_AFE_MODEL"
#endif

/* Compatibility with the first HS-D011 implementation. New code should use
 * BMS_AFE_MODEL rather than coupling simulation state to board wiring.
 */
#if (BMS_AFE_MODEL == BMS_AFE_MODEL_SIMULATED)
#define BMS_AFE_SIMULATION_ENABLE 1u
#else
#define BMS_AFE_SIMULATION_ENABLE 0u
#endif

#ifndef BMS_RS485_ENABLE
#define BMS_RS485_ENABLE 0u
#endif

#ifndef BMS_AFE_POLL_PERIOD_US
#define BMS_AFE_POLL_PERIOD_US 100000u
#endif

#ifndef BMS_PROTECT_OPPOSITE_REOPEN_ENABLE
#define BMS_PROTECT_OPPOSITE_REOPEN_ENABLE 1u
#endif

/* Serial low-power policy: while serial is active, BLE suspend is vetoed so
 * UART/DMA can communicate normally. After 3 seconds without serial activity,
 * UART RX is released and the physical RX pin becomes a low-level wake pad.
 * The wake frame may be lost by design; the next Modbus request is processed
 * after UART/DMA are restored. This policy is common to direct UART and RS485.
 */
#ifndef BMS_SERIAL_PM_ENABLE
#define BMS_SERIAL_PM_ENABLE            1u
#endif
#ifndef BMS_SERIAL_IDLE_SLEEP_MS
#define BMS_SERIAL_IDLE_SLEEP_MS        3000u
#endif
#ifndef BMS_SERIAL_WAKE_LEVEL
#define BMS_SERIAL_WAKE_LEVEL           Level_Low
#endif

#if BMS_SERIAL_ENABLE && BMS_SERIAL_PM_ENABLE
#ifndef BMS_SERIAL_TX_GPIO
#error "BMS_SERIAL_TX_GPIO is required when serial low-power is enabled"
#endif
#ifndef BMS_SERIAL_RX_GPIO
#error "BMS_SERIAL_RX_GPIO is required when serial low-power is enabled"
#endif
#endif

/* The production SOC estimator is not integrated yet. A zero/uninitialized SOC
 * must therefore never close the discharge MOS. Enable this only when SOC has
 * a validity model and real coulomb/OCV estimation behind it.
 */
#ifndef BMS_SOC_PROTECT_ENABLE
#define BMS_SOC_PROTECT_ENABLE 0u
#endif

#endif /* BMS_BOARD_H_ */
