# Serial wake + 3-second idle low-power test

## Policy

The serial transport intentionally does **not** guarantee that the first Modbus RTU request after sleep is preserved.

Runtime policy:

1. UART/RS485 is `ACTIVE` after boot and after any serial wake.
2. While `ACTIVE`, BLE suspend is vetoed so UART RX/TX DMA can communicate normally.
3. Every UART RX/TX activity restarts a 3-second idle timer.
4. After 3 seconds with no serial activity and no RX/TX work pending, UART/DMA is stopped.
5. PC3 is remuxed from UART RX to GPIO input and armed as a low-level wake pad.
6. BLE suspend is restored; the existing BMS application wake deadline still wakes the MCU for the 100 ms AFE/protection schedule.
7. A start bit on PC3 wakes the MCU. The wake frame may be lost.
8. UART/DMA is restored in the normal main-loop context, and the next Modbus request must communicate normally.

This is common to both current boards:

- `legacy-309`: PC2 TX, PC3 RX, direct UART, no DE.
- `hs-d011`: PC2 TX, PC3 RX/RO, PA1 DE; DE stays low while serial is sleep-armed.

## Configuration

Common defaults in `bms_board.h`:

```c
BMS_SERIAL_PM_ENABLE      = 1
BMS_SERIAL_IDLE_SLEEP_MS  = 3000
BMS_SERIAL_WAKE_LEVEL     = Level_Low
```

The board profile must also provide both UART mux identities and GPIO identities:

```c
BMS_SERIAL_TX_PIN
BMS_SERIAL_RX_PIN
BMS_SERIAL_TX_GPIO
BMS_SERIAL_RX_GPIO
```

## Diagnostic registers

A new read-only Modbus block is available at `0xD140`:

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

Use **BLE Modbus** to observe `D140..D147` while testing serial sleep. Polling this block over UART itself counts as serial activity and would keep the serial transport ACTIVE.

## Bench procedure for TLSR8251 + SH367309

Build the real 309 profile:

```powershell
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
python bms_tools/bms.py verify
```

Then flash the canonical `825x_ble_sample.bin`.

Test sequence:

1. Immediately after boot, read `D140..D147` through BLE. `D142.bit0` should be 1 (ACTIVE).
2. Do not send any UART request for more than 3 seconds. Observe through BLE that `D142.bit1` becomes 1 and `sleep_count` increments once. Measuring current should show the return to the normal BLE/BMS suspend regime.
3. Send one UART Modbus request. It is allowed to time out because it is the wake frame.
4. Retry the UART request. The second request must receive a normal Modbus response.
5. Read `D140..D147` through BLE: `wake_count` must have incremented and `D142.bit0` should be 1 while the 3-second active window is running.
6. Continue UART polling at intervals shorter than 3 seconds; the link must remain active and responsive.
7. Stop UART polling again for more than 3 seconds. Observe through BLE that `sleep_count` increments again and low-power current returns.
8. Repeat wake/sleep cycles at least 100 times before considering the behavior bench-validated.

If BLE is not available during a UART-only bench test, infer the transition from current consumption and then read the counters after the UART has been woken; do not continuously read `D140..D147` over UART while trying to prove the 3-second idle transition.

## Acceptance

- UART/RS485 communication works normally while ACTIVE.
- The first request after sleep may be lost; the following retry works.
- No serial activity for 3 seconds returns the transport to WAKE_ARMED.
- TX is never suspended mid-frame.
- AFE/protection sampling continues on its existing application deadline while serial is WAKE_ARMED.
- BLE scan/connect/communication remains functional after repeated serial wake/sleep cycles.
- HS-D011 must additionally verify DE=0 while WAKE_ARMED when target hardware is available.
