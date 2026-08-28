# HS-D011 pre-hardware simulation test

The branch currently defaults to **simulation mode** because the matching HS-D011/SH3673510 board is not yet available.

## Switch

`tc_ble_single_sdk/vendor/ble_sample/hs_d011_board.h`:

```c
#define BMS_AFE_SIMULATION_ENABLE 1u
```

- `1`: pre-hardware bench mode. Do not initialize SH3673510 SPI, do not drive HS-D011 board GPIO, and do not initialize the HS-D011 RS485 UART. BLE/SPP + Modbus remain active.
- `0`: real HS-D011 hardware mode. SH3673510 and HS-D011 IO are initialized normally.

Always run a clean rebuild after changing this switch.

## BLE identity

Default BLE name:

```text
BT_d011_default
```

The existing Nordic-UART-style Telink SPP UUIDs and Modbus-over-BLE transport are unchanged.

## Simulated battery data

The AFE adapter reports type `0xFFFF` (simulated) and provides 10-cell data every 100 ms:

- cell baseline: approximately 3300..3312 mV, with a slow 0..4 mV common variation;
- pack voltage: approximately 33.0 V;
- cell delta: 8 mV;
- current: cycles through 0 mA, +650 mA discharge, 0 mA, -450 mA charge;
- TS1: 25.0 C;
- TS2: 25.3 C;
- TS3: not populated;
- TS4/MOS: 27.8 C;
- AFE fault bits: 0;
- SOC: 75%;
- SOH: 100%;
- nominal/full/current capacity: 50.00 / 49.00 / 36.75 Ah;
- cycle count: 12.

The simulated AFE exposes no native hardware-protection capability. The common software-protection layer still evaluates the simulated measurements, while MOS writes and fault-clear commands are acknowledged without touching hardware.

## Intended bench board

This image can be used on an older TLSR8251 BMS board (for example an 8251 + SH367309 board) to verify the MCU/BLE path. Because its AFE and pin map differ from HS-D011, simulation mode deliberately avoids HS-D011-specific AFE, GPIO and RS485 initialization.

## Build

```powershell
python bms_tools/bms.py rebuild --jobs 1
python bms_tools/bms.py verify
```

Use the post-processed image:

```text
tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

## Minimum BLE test

1. Scan for `BT_d011_default`.
2. Connect and enable notifications on the existing SPP notify characteristic.
3. Send the existing Modbus RTU read commands through the SPP write characteristic.
4. Read `0xD000` legacy BMS data and confirm the 10 simulated cell voltages, temperatures, SOC and alternating current.
5. Read `0xD120` compact realtime data and confirm it changes over time.
6. Read `0x2000` discovery metadata; AFE type should report simulated `0xFFFF`.
7. Optionally test OTA separately after ordinary BLE/Modbus reads are stable.

Before first bring-up on the real HS-D011 board, set `BMS_AFE_SIMULATION_ENABLE` to `0`, clean-rebuild, and then validate SH3673510 SPI, measurements, MOS control and protection on hardware.
