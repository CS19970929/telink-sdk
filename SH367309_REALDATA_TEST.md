# Multi-AFE integration and SH367309 real-data bring-up

## Selection model

`vendor/ble_sample/bms_board.h` separates physical board wiring from AFE protocol logic. `bms_tools/bms.py` now selects both dimensions explicitly at compile time.

Current default remains:

- board: `legacy-309` / `BMS_BOARD_PROFILE_LEGACY_309`
- AFE: `sh367309` / `BMS_AFE_MODEL_SH367309`
- AFE mode: REAL
- cells: 10S
- SH367309 bus: Telink I2C C0/C1, address `0x34`, 100 kHz
- effective shunt: 1 mOhm (reference D3PRO: two 2 mOhm shunts in parallel)
- serial Modbus: direct UART PC2/PC3, 115200 8N1, no DE pin
- BLE Modbus: enabled as before

List all supported combinations with:

```powershell
python bms_tools/bms.py profiles
```

The current valid combinations are `legacy-309 + sh367309`, `legacy-309 + mock`, `hs-d011 + sh3673510`, and `hs-d011 + mock`. Invalid board/AFE combinations are rejected before compilation.

## What was reused from the proven SH367309 project

The driver follows the existing `telink-new-sdk-b85` implementation for the wire protocol and conversions rather than copying its application-coupled data layer:

- CRC-8 polynomial `0x07`
- read CRC stream: slave address, register, length, read address, data
- write CRC stream: slave address, register, data
- RAM telemetry window `0x40..0x71`
- CELL1..CELL16 at `0x4E..0x6D`, conversion `raw * 5 / 32` mV
- current source CADC at `0x6E`, bit15 sign convention (1=discharge, 0=charge)
- TEMP1..TEMP3 at `0x46/0x48/0x4A`
- TR at `0x19`, reference resistance `680 + 5*(TR & 0x7f)`
- BSTATUS1/BSTATUS2 native protection-state mapping
- direct UART PC2/PC3 at 115200 8N1

The old global `g_stCellInfoReport`, fault recorder, SOC, Modbus and parameter code are intentionally not imported into the chip driver. `sh367309.c` only speaks to the AFE and produces a typed sample; `bms_afe.c` normalizes that sample for the common BMS layer.

## First bench phase is intentionally read-mostly

For the first real-board validation:

```c
BMS_SH309_RESET_ON_INIT       0
BMS_SH309_MOS_CONTROL_ENABLE  0
```

The firmware does not reset/program SH367309 MTP and does not take over FET control. Existing AFE hardware protection configuration remains authoritative. Common software protection evaluates the real sample, but the SH367309 adapter does not advertise `BMS_AFE_FEAT_MOS_CONTROL` yet.

This is deliberate. First establish that the new driver reproduces the proven firmware's cell voltage, temperature, current sign/magnitude and fault status. Then enable FET control and parameter projection as a separate, testable step.

## Build and real-data test

Use the explicit profile so the build log and manifest state unambiguously that this is a real SH367309 image:

```powershell
git pull
python bms_tools/bms.py profiles
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
python bms_tools/bms.py map
python bms_tools/bms.py verify
```

`rebuild` prints the selected board, AFE and `REAL`/`SIMULATED` mode before compilation. The profile-specific artifacts are placed under:

```text
tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/legacy-309_sh367309/
```

After the Telink post-check succeeds, the same verified image is also copied to the historical canonical burn path:

```text
tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

The manifest records `board=legacy-309`, `afe=sh367309` and `afe_mode=REAL`, and `verify` also checks source/build-input hashes so an artifact cannot silently be mistaken for another profile.

Flash the canonical `825x_ble_sample.bin` to the TLSR8251 + SH367309 board. Real data can then be checked through either BLE SPP Modbus or the direct PC2/PC3 UART Modbus transport.

Useful legacy real-time registers:

- `0xD000..0xD009`: cell 1..10, mV
- `0xD020`: maximum cell mV
- `0xD021`: minimum cell mV
- `0xD024`: cell delta mV
- `0xD025`: pack voltage / 10
- `0xD026`, `0xD027`: active battery temperatures, encoded as `(degC + 40) * 10`
- `0xD032`: charge current, 0.1 A units
- `0xD033`: discharge current, 0.1 A units

Serial settings: slave address `0x01`, 115200 baud, 8 data bits, no parity, 1 stop bit. The direct legacy UART does not drive PA1. HS-D011 continues to use PA1 only as its RS485 direction pin.

Acceptance for the first bench pass:

1. AFE stays online continuously; no periodic CRC/read failures.
2. Ten cell voltages track a DMM/reference firmware within the expected AFE accuracy.
3. Pack voltage equals the cell sum within integer-conversion tolerance.
4. TEMP1/TEMP2 track the reference firmware/thermometer.
5. No-current offset is recorded; charge reports negative `current_ma`, discharge positive internally, while D000 keeps the legacy split charge/discharge registers.
6. Apply a known small charge/discharge current and confirm scaling before any protection/MOS-control work.
7. Native BSTATUS protection bits agree with the known-good firmware when a safe fault-injection test is performed.
8. Send the same Modbus read through BLE and PC2/PC3 UART and confirm the register payloads match.

## Low-power note for serial validation

The current project keeps Telink BLE suspend enabled and arms the BMS sampling deadline. That protects the 100 ms BMS scheduler, but it is not yet proof that an asynchronous UART request can wake the MCU without losing its first byte. During initial serial validation, keep the device active/connected and verify repeated requests. UART/low-power arbitration remains a separate acceptance item before release.

## Switching profiles

Examples:

```powershell
# Current real 309 board
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4

# Same legacy board with fake AFE data
python bms_tools/bms.py rebuild --board legacy-309 --afe mock --jobs 4

# HS-D011 real SH3673510 board
python bms_tools/bms.py rebuild --board hs-d011 --afe sh3673510 --jobs 4

# HS-D011 application stack without touching the real AFE
python bms_tools/bms.py rebuild --board hs-d011 --afe mock --jobs 4
```

Each combination has its own object/output directory, preventing incremental builds from reusing objects compiled with a different AFE macro.

## Next SH367309 phase

After telemetry passes, enable and verify in this order: read-modify-write MOS control with CONF readback; native protection/MTP parameter projection with VPRO gating and per-byte verification; fault-clear semantics from verified documentation/bench behavior; then UART wake/low-power arbitration without losing the first Modbus byte.
