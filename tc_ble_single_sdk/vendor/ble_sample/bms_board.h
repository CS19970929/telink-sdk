#ifndef BMS_BOARD_H_
#define BMS_BOARD_H_

#include "tl_common.h"
#include "drivers.h"

/* Board and AFE are independent compile-time selections. A board profile owns
 * only physical wiring/BOM facts; the AFE model owns register/protocol logic.
 * This keeps bms_project/modbus/protection independent of a specific AFE.
 */
#define BMS_BOARD_PROFILE_HS_D011       1u
#define BMS_BOARD_PROFILE_LEGACY_309    2u

#define BMS_AFE_MODEL_SIMULATED         0u
#define BMS_AFE_MODEL_SH367309          1u
#define BMS_AFE_MODEL_SH3673510         2u

/* Current bench target: the existing TLSR8251 + SH367309 board. Override from
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

/* Serial power policy.
 *
 * ACTIVE deliberately preserves the exact UART/DMA flow that has been bench
 * verified against the legacy SH367309 project and vetoes BLE Suspend.
 * After 3 seconds without RX/TX activity, plus a short boundary guard, the
 * transport leaves UART once and permits normal BLE/BMS Suspend again.
 *
 * BMS_SERIAL_PM_VARIANT exists specifically for controlled power A/B tests.
 * Normal builds default to CURRENT, which is the already bench-validated
 * RISC0+PAD wake implementation with TX held HIGH while serial is asleep.
 */
#ifndef BMS_SERIAL_KEEP_AWAKE
#define BMS_SERIAL_KEEP_AWAKE           1u
#endif
#ifndef BMS_SERIAL_PM_ENABLE
#define BMS_SERIAL_PM_ENABLE            1u
#endif
#ifndef BMS_SERIAL_IDLE_SLEEP_MS
#define BMS_SERIAL_IDLE_SLEEP_MS        3000u
#endif
#ifndef BMS_SERIAL_SLEEP_GUARD_MS
#define BMS_SERIAL_SLEEP_GUARD_MS       50u
#endif
#ifndef BMS_SERIAL_WAKE_LEVEL
#define BMS_SERIAL_WAKE_LEVEL           Level_Low
#endif
#ifndef BMS_SERIAL_RX_SLEEP_PULL
#define BMS_SERIAL_RX_SLEEP_PULL        PM_PIN_PULLUP_1M
#endif

#define BMS_SERIAL_PM_VARIANT_CURRENT       0u /* RISC0 + PAD, TX held HIGH: current validated behavior */
#define BMS_SERIAL_PM_VARIANT_DUAL_HIZ      1u /* RISC0 + PAD, TX high-Z */
#define BMS_SERIAL_PM_VARIANT_PAD_HIZ       2u /* PAD only, TX high-Z: preferred low-power candidate */
#define BMS_SERIAL_PM_VARIANT_NOWAKE_HIZ    3u /* no serial wake, TX high-Z: power-reference firmware */

#ifndef BMS_SERIAL_PM_VARIANT
#define BMS_SERIAL_PM_VARIANT BMS_SERIAL_PM_VARIANT_CURRENT
#endif

#if (BMS_SERIAL_PM_VARIANT > BMS_SERIAL_PM_VARIANT_NOWAKE_HIZ)
#error "Unsupported BMS_SERIAL_PM_VARIANT"
#endif

#if (BMS_SERIAL_PM_VARIANT == BMS_SERIAL_PM_VARIANT_CURRENT) || \
    (BMS_SERIAL_PM_VARIANT == BMS_SERIAL_PM_VARIANT_DUAL_HIZ)
#define BMS_SERIAL_PM_USE_RISC0_WAKE    1u
#else
#define BMS_SERIAL_PM_USE_RISC0_WAKE    0u
#endif

#if (BMS_SERIAL_PM_VARIANT == BMS_SERIAL_PM_VARIANT_CURRENT) || \
    (BMS_SERIAL_PM_VARIANT == BMS_SERIAL_PM_VARIANT_DUAL_HIZ) || \
    (BMS_SERIAL_PM_VARIANT == BMS_SERIAL_PM_VARIANT_PAD_HIZ)
#define BMS_SERIAL_PM_USE_PAD_WAKE      1u
#else
#define BMS_SERIAL_PM_USE_PAD_WAKE      0u
#endif

#if (BMS_SERIAL_PM_VARIANT == BMS_SERIAL_PM_VARIANT_CURRENT)
#define BMS_SERIAL_PM_TX_SLEEP_HIZ      0u
#else
#define BMS_SERIAL_PM_TX_SLEEP_HIZ      1u
#endif

#if BMS_SERIAL_ENABLE && BMS_SERIAL_PM_ENABLE
#ifndef BMS_SERIAL_TX_GPIO
#error "BMS_SERIAL_TX_GPIO is required when serial PM is enabled"
#endif
#ifndef BMS_SERIAL_RX_GPIO
#error "BMS_SERIAL_RX_GPIO is required when serial PM is enabled"
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