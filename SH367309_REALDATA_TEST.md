# Multi-AFE integration and SH367309 real-data bring-up

## Selection model

`vendor/ble_sample/bms_board.h` separates physical board wiring from AFE protocol logic.

Current default:

- board: `BMS_BOARD_PROFILE_LEGACY_309`
- AFE: `BMS_AFE_MODEL_SH367309`
- cells: 10S
- SH367309 bus: Telink I2C C0/C1, address `0x34`, 100 kHz
- effective shunt: 1 mOhm (reference D3PRO: two 2 mOhm shunts in parallel)
- physical RS485: disabled for this profile because the legacy board uses PA1 as `MCC_C`, not HS-D011 RS485 DE
- BLE Modbus: enabled as before

To return to HS-D011, select `BMS_BOARD_PROFILE_HS_D011`; its default AFE is SH3673510. `BMS_AFE_MODEL_SIMULATED` remains available independently of the board profile.

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

The old global `g_stCellInfoReport`, fault recorder, SOC, Modbus and parameter code are intentionally not imported into the chip driver. `sh367309.c` only speaks to the AFE and produces a typed sample; `bms_afe.c` normalizes that sample for the common BMS layer.

## First bench phase is intentionally read-mostly

For the first real-board validation:

```c
BMS_SH309_RESET_ON_INIT      0
BMS_SH309_MOS_CONTROL_ENABLE 0
```

The firmware does not reset/program SH367309 MTP and does not take over FET control. Existing AFE hardware protection configuration remains authoritative. Common software protection still evaluates the real sample but the SH367309 adapter does not advertise `BMS_AFE_FEAT_MOS_CONTROL` yet.

This is deliberate. First establish that the new driver reproduces the proven firmware's cell voltage, temperature, current sign/magnitude and fault status. Then enable FET control and parameter projection as a separate, testable step.

## Build and real-data test

```powershell
git pull
python bms_tools/bms.py rebuild --jobs 1
python bms_tools/bms.py map
python bms_tools/bms.py verify
```

Flash the generated `825x_ble_sample.bin` to the TLSR8251 + SH367309 board. Connect over the existing BLE SPP service and read Modbus holding registers.

Useful legacy real-time registers:

- `0xD000..0xD009`: cell 1..10, mV
- `0xD020`: maximum cell mV
- `0xD021`: minimum cell mV
- `0xD024`: cell delta mV
- `0xD025`: pack voltage / 10
- `0xD026`, `0xD027`: active battery temperatures, encoded as `(degC + 40) * 10`
- `0xD032`: charge current, 0.1 A units
- `0xD033`: discharge current, 0.1 A units

Acceptance for the first bench pass:

1. AFE stays online continuously; no periodic CRC/read failures.
2. Ten cell voltages track a DMM/reference firmware within the expected AFE accuracy.
3. Pack voltage equals the cell sum within integer-conversion tolerance.
4. TEMP1/TEMP2 track the reference firmware/thermometer.
5. No-current offset is recorded; charge reports negative `current_ma`, discharge positive internally, while D000 keeps the legacy split charge/discharge registers.
6. Apply a known small charge/discharge current and confirm scaling before any protection/MOS-control work.
7. Native BSTATUS protection bits agree with the known-good firmware when a safe fault-injection test is performed.

## Next SH367309 phase

After telemetry passes, enable and verify in this order: read-modify-write MOS control with CONF readback; native protection/MTP parameter projection with VPRO gating and per-byte verification; fault-clear semantics from verified documentation/bench behavior; then physical UART/RS485 only for board profiles whose transceiver wiring is explicitly defined.
