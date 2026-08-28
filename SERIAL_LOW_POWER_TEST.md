# Serial wake + 3-second idle low-power test

## Stable runtime policy

The active UART path is the already bench-validated 115200 8N1 DMA implementation. Low-power logic is only allowed to touch the transport after the final UART activity has been idle for 3 seconds plus a 50 ms guard.

Normal/current behavior:

1. UART/RS485 is `ACTIVE` after boot and after any serial wake.
2. While `ACTIVE`, BLE suspend is vetoed so UART RX/TX DMA can communicate normally.
3. Every UART RX/TX activity restarts a 3-second idle timer.
4. After 3 seconds plus the quiet guard, with no RX/TX work pending, UART/DMA is stopped.
5. PC3 is remuxed from UART RX to GPIO input.
6. BLE/BMS suspend is restored.
7. A serial start bit can wake the MCU according to the selected test variant.
8. The wake frame may be lost; subsequent Modbus requests must communicate normally.

The active UART/DMA flow is intentionally identical across all power-test variants. Only the sleep-side TX drive and serial wake mechanisms change.

This is common to both current boards:

- `legacy-309`: PC2 TX, PC3 RX, direct UART, no DE.
- `hs-d011`: PC2 TX, PC3 RX/RO, PA1 DE; DE stays low while serial is sleep-armed.

## One-command firmware matrix

For the current TLSR8251 + SH367309 bench target:

```powershell
python bms_tools/serial_pm_matrix.py
```

Equivalent explicit command:

```powershell
python bms_tools/serial_pm_matrix.py --board legacy-309 --afe sh367309 --jobs 1
```

The helper performs four clean rebuilds, runs the normal Telink post-check for each build, and writes named test firmware under:

```text
tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/serial_pm_matrix/legacy-309_sh367309/
```

It builds the `CURRENT` variant last, so the usual canonical `825x_ble_sample.bin` is left on the normal bench-validated configuration after the command finishes.

## A/B variants

| File | Variant ID | PAD wake | RISC0 | TX while asleep | Expected serial wake | Purpose |
| --- | ---: | --- | --- | --- | --- | --- |
| `A_current-dual-txhigh.bin` | 0 | yes | yes | driven HIGH | yes | Exact current implementation; reproduces the present ~1 mA result |
| `B_dual-hiz.bin` | 1 | yes | yes | high-Z | yes | Isolates any cost caused by holding TX HIGH |
| `C_pad-hiz.bin` | 2 | yes | no | high-Z | yes | Preferred candidate if RISC0 is responsible for excess current |
| `D_no-wake-hiz.bin` | 3 | no | no | high-Z | **no** | Power reference: isolates the cost of serial wake circuitry itself |

`D_no-wake-hiz.bin` is intentionally not a usable final communication firmware: after UART has been idle for 3 seconds and enters serial sleep, UART traffic cannot wake it. Use it only to measure the low-power floor, then power-cycle/reflash.

## Compile-time selection

Normal builds still default to variant 0, so the ordinary command remains unchanged:

```powershell
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 1
```

The matrix helper sets `BMS_SERIAL_PM_VARIANT` for each clean build:

```text
0 = CURRENT: RISC0 + PAD, TX HIGH
1 = DUAL_HIZ: RISC0 + PAD, TX high-Z
2 = PAD_HIZ: PAD only, TX high-Z
3 = NOWAKE_HIZ: no serial wake, TX high-Z
```

## Diagnostic registers

The read-only serial PM block is at `0xD140`.

| Register | Meaning |
| --- | --- |
| `D140` | magic `0x5350` (`SP`) |
| `D141` | version `0x0002` |
| `D142` | state flags: bit0 ACTIVE, bit1 WAKE_ARMED, bit2 WAKE_PENDING, bit3 PM enabled |
| `D143` | idle timeout in ms (`3000`) |
| `D144` | sleep_count low word |
| `D145` | sleep_count high word |
| `D146` | wake_count low word |
| `D147` | wake_count high word |
| `D148` | compiled PM variant ID: 0/1/2/3 |
| `D149` | config flags: bit0 PAD wake, bit1 RISC0 wake, bit2 TX high-Z |

Use BLE Modbus to read this block while measuring serial sleep. Polling it over UART itself refreshes the serial activity timer.

Expected `D148/D149` pairs:

| Variant | D148 | D149 |
| --- | ---: | ---: |
| A current | `0x0000` | `0x0003` |
| B dual-hiz | `0x0001` | `0x0007` |
| C pad-hiz | `0x0002` | `0x0005` |
| D no-wake-hiz | `0x0003` | `0x0004` |

## Measurement procedure

Keep the electrical setup identical for all four firmware images. Previous bench testing already showed that physically disconnecting the UART cable does not change the approximately 1 mA result, so the matrix is aimed at internal sleep/wake behavior rather than external backfeed.

For each image:

1. Flash the named BIN.
2. Confirm normal boot and, for A/B/C, normal continuous UART communication.
3. Stop UART traffic for at least 5 seconds.
4. Measure steady low-power current, not the short transition peak.
5. Read `D148/D149` over BLE to confirm the flashed variant if needed.
6. For A/B/C, send UART traffic after sleep. The first request may be lost; retries must restore stable communication.
7. Record the low-power current and whether serial wake/recovery works.

Recommended result table:

| Variant | Low-power current | Serial wake | Continuous communication after wake |
| --- | ---: | --- | --- |
| A current-dual-txhigh |  |  |  |
| B dual-hiz |  |  |  |
| C pad-hiz |  |  |  |
| D no-wake-hiz |  | N/A by design | N/A |

## Interpretation

- If A and B are essentially equal, TX driven HIGH is not the cause.
- If B is high but C falls back near the old `<500 uA` level, RISC0/falling-edge GPIO interrupt is the main extra load.
- If C is still high but D returns near `<500 uA`, PAD serial wake or its interaction with BLE suspend is the main extra load.
- If D is still around 1 mA, the increase is not caused by the serial wake mechanisms and the next comparison should move to the broader BLE/BMS suspend policy.
- If C gives low power and reliable wake, `PAD only + TX high-Z` is the preferred final policy.

Do not change the already validated ACTIVE UART/DMA path while running this matrix. The purpose of the test is to isolate sleep-side power cost only.
