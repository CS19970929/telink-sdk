# BMS Low-Power Scheduler

## Goal

Keep Telink BLE suspend enabled during advertising, connection, normal charge and normal discharge, while preventing BLE connection interval/slave latency from delaying BMS sampling and protection tasks.

The BLE controller remains responsible for the physical suspend/wakeup mechanism. The BMS layer supplies an application deadline with `bls_pm_setAppWakeupLowPower()`.

## Current policy

- `BLE_APP_PM_ENABLE = 1`: BLE suspend remains enabled.
- `PM_DEEPSLEEP_RETENTION_ENABLE = 0`: deep-retention sleep is not used by the current BMS runtime.
- `TEST_CONN_CURRENT_ENABLE = 1`: the generic Telink sample 60-second deep-sleep path remains suppressed.
- `BMS_AFE_POLL_PERIOD_US = 100000`: the active BMS sample/protection deadline is currently 100 ms.
- Before each following BLE PM cycle, the BMS layer arms an application wake deadline no later than the next AFE sample deadline.
- BLE may wake the MCU earlier for radio events; it must not use a long connection/slave-latency sleep to postpone the BMS deadline.

## Scheduler diagnostics

`bms_project_state_t` now contains:

- `afe_sample_dt_us`: measured elapsed time between AFE sample attempts.
- `scheduler_overrun_count`: increments when elapsed time exceeds the nominal sample period plus the configured tolerance.
- `pm_app_wakeup_tick`: current absolute Telink system-timer deadline armed for application wake.

These values are internal diagnostics for now and are not part of the stable Modbus register map.

## SOC rule

The current branch still uses simulated/static SOC; this change does not introduce the final coulomb-counting algorithm.

When the SOC module is added, it must not assume that one callback equals exactly 200 ms. The scheduler should target the required SOC period, while the integrator uses the measured elapsed time (`dt`) between valid current samples/updates. That prevents BLE scheduling, flash operations or occasional task overruns from silently losing charge integration.

The future scheduler should choose the earliest deadline among at least:

- AFE/protection sampling
- SOC integration
- parameter/storage maintenance
- application communication deadlines

Deep sleep for long idle/deep-discharge states is a separate BMS state-machine policy and must not be enabled merely because the BLE sample has a generic idle timeout.

## Regression baseline

BLE/SPP/Modbus behavior before this scheduler work is frozen on branch:

`baseline/hs-d011-ble-mock-v1`

at commit:

`3694df69910d8483ed94f49d3daf612489f55533`

If BLE communication regresses, compare against that baseline first.

## Bench test before target PCB is available

Use the mock AFE image on the existing TLSR8251 board. Confirm:

1. `BT_d011_default` is still discoverable and connectable.
2. SPP/Modbus communication remains functional while BLE PM is enabled.
3. Connection remains stable for several minutes.
4. If current measurement equipment is available, compare average current with the previous baseline. The new image is expected to wake at least every ~100 ms for the BMS deadline, so connected idle current can be higher than a BLE-only image that uses long slave latency.

Target-board tests must later verify actual AFE sampling cadence, protection latency, low-power current and SOC error across repeated suspend/wakeup cycles.
