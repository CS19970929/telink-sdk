# Serial wake + 3-second idle low-power test

## Policy

The serial transport intentionally does **not** guarantee that the first Modbus RTU request after sleep is preserved.

Runtime policy:

1. UART/RS485 is `ACTIVE` after boot and after any serial wake.
2. While `ACTIVE`, the UART/DMA flow is the same sequence already verified on the legacy SH367309 project, and BLE Suspend is vetoed.
3. Every UART RX/TX activity restarts a 3-second idle timer.
4. After 3 seconds with no serial activity, plus a 50 ms quiet guard, and with no RX/TX work pending, UART/DMA is stopped once.
5. PC3 is remuxed from UART RX to GPIO input, weakly pulled high, and armed for falling-edge RISC0 plus low-level PAD wake.
6. BLE/BMS Suspend policy is restored. The existing BMS application wake deadline may still wake the MCU for AFE/protection work; this does not restore UART.
7. A serial start bit on PC3 wakes the MCU. RISC0 detects activity while the CPU is already awake; PAD wake covers BLE Suspend/deep-sleep entry.
8. The wake handler only posts a pending flag. UART/DMA are restored later in normal project-loop context using the known-good active initialization sequence.
9. The wake frame may be lost. The next Modbus retry must communicate normally.
10. Any restored UART activity starts a new 3-second active window; after the final activity the transport returns to low power again.

This is common to both current boards:

- `legacy-309`: PC2 TX, PC3 RX, direct UART, no DE.
- `hs-d011`: PC2 TX, PC3 RX/RO, PA1 DE; DE stays low while serial is sleep-armed.

## Configuration

Common defaults in `bms_board.h`:

```c
BMS_SERIAL_PM_ENABLE         = 1
BMS_SERIAL_KEEP_AWAKE        = 1   // applies while ACTIVE
BMS_SERIAL_IDLE_SLEEP_MS     = 3000
BMS_SERIAL_SLEEP_GUARD_MS    = 50
BMS_SERIAL_WAKE_LEVEL        = Level_Low
BMS_SERIAL_RX_SLEEP_PULL     = PM_PIN_PULLUP_1M
```

The board profile must provide both UART mux identities and GPIO identities:

```c
BMS_SERIAL_TX_PIN
BMS_SERIAL_RX_PIN
BMS_SERIAL_TX_GPIO
BMS_SERIAL_RX_GPIO
```

## PM arbitration

`blt_pm_proc()` runs before `bms_project_process()` in the current main loop. The serial layer therefore owns the final PM decision for the next LinkLayer iteration:

- `ACTIVE`: force `SUSPEND_DISABLE`, and refresh the BLE sample's advertising/connection inactivity timers so its 60-second deep-sleep policy cannot interrupt an active UART session.
- `WAKE_ARMED`: do not veto BLE PM; the normal application mask is restored and PC3 remains armed as a wake source.

The serial suspend-enter callback wraps the original `task_sleep_enter()` and adds `PM_WAKEUP_PAD` while `WAKE_ARMED`. GPIO early wake and RISC0 only post `wake_pending`; UART/DMA restoration is not performed in interrupt/callback context.

## Diagnostic registers

A read-only Modbus block is available at `0xD140`:

| Register | Meaning |
| --- | --- |
| `D140` | magic `0x5350` (`SP`) |
| `D141` | version `0x0001` |
| `D142` | flags: bit0 ACTIVE, bit1 WAKE_ARMED, bit2 WAKE_PENDING, bit3 PM enabled |
| `D143` | idle timeout in ms (default 3000) |
| `D144` | sleep_count low word |
| `D145` | sleep_count high word |
| `D146` | wake_count low word |
| `D147` | wake_count high word |

Use **BLE Modbus** to observe `D140..D147` while testing serial sleep. Polling this block over UART itself counts as serial activity and keeps the serial transport ACTIVE.

## Bench procedure for TLSR8251 + SH367309

Build the real 309 profile:

```powershell
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
python bms_tools/bms.py verify
```

Then flash the canonical `825x_ble_sample.bin`.

Test sequence:

1. Immediately after boot, read `D140..D147` through BLE. `D142.bit0` should be 1 (`ACTIVE`).
2. Poll UART at 200-500 ms for several minutes. Communication must remain stable and `sleep_count` must not increase.
3. Stop all UART requests. After about 3.05 seconds, observe through BLE that `D142.bit1` becomes 1 and `sleep_count` increments once. Current consumption should return to the normal BLE/BMS low-power regime.
4. Wait 10 seconds, then send one UART Modbus request. It is allowed to time out because it is the wake frame.
5. Retry the UART request. The following request must receive a normal Modbus response.
6. Read `D140..D147` through BLE: `wake_count` must have incremented and `D142.bit0` should be 1 during the new active window.
7. Continue UART polling at intervals shorter than 3 seconds; the link must remain active and responsive without additional sleep transitions.
8. Stop UART polling again for more than 3 seconds. `sleep_count` must increment again and low-power current must return.
9. Repeat the same sequence after 60 seconds of serial inactivity. A request must still wake the MCU; if the BLE sample has progressed to full deep sleep, the wake may reboot the application and the following request must work after normal startup.
10. Repeat wake/sleep cycles at least 100 times before considering the behavior bench-validated; use 1000 cycles for release qualification.

If BLE is not available during a UART-only bench test, infer the transition from current consumption and then read the counters after UART has been woken; do not continuously read `D140..D147` over UART while trying to prove the 3-second idle transition.

## Acceptance

- UART/RS485 communication is stable while `ACTIVE`.
- Continuous UART activity cannot be interrupted by the BLE sample's Suspend or 60-second deep-sleep timeout.
- No serial activity for 3 seconds plus the 50 ms quiet guard returns the transport to `WAKE_ARMED`.
- TX is never suspended mid-frame.
- The first request after sleep may be lost; the following retry works.
- BMS AFE/protection scheduling continues while serial is `WAKE_ARMED`.
- BLE scan/connect/communication remains functional after repeated serial wake/sleep cycles.
- HS-D011 must additionally verify DE=0 while `WAKE_ARMED` when target hardware is available.
