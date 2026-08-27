# HS-D011-10S50A-V1 / TLSR8251 + SH3673510

This branch implements the HS-D011 board on top of `sdk-template` while keeping the established Telink BMS BLE/Modbus interface where possible.

## Hardware mapping

| Function | TLSR8251 | Notes |
|---|---|---|
| SH3673510 SDO -> MCU MISO | PB6 | HW SPI group B6/B7/D2/D7 |
| MCU MOSI -> SH3673510 SDI | PB7 | HW SPI |
| SH3673510 SCK | PD7 | MODE3, 1 MHz |
| SH3673510 CS | PD2 | active low |
| SH3673510 ALARM | PC0 | input |
| SH3673510 RESET | PC1 | AFE open-drain output, MCU input only |
| RS485 TX | PC2 | UART TX |
| RS485 RX | PC3 | UART RX |
| RS485 DE/RE | PA1 | high=TX, low=RX |
| DI1 | PA0 | input |
| INT-WK-MCU | PB1 | input |
| HT-CHG | PB4 | output |
| HT-RF-EN | PB5 | output |
| DB-LED1 | PC4 | output |
| CMNT-EN | PD4 | output |
| CMNT-WK | PD3 | input |

The schematic uses eight 2 mOhm shunts in parallel, therefore the effective current-sense resistance is configured as 0.25 mOhm (`BMS_RSENSE_UOHM=250`).

External temperature channels are TS1, TS2 and TS4-MOS. TS3 is not populated. The fitted sensors are 10K/B3435 NTCs; the firmware uses the previous BMS 10K/B3435 table.

## SH3673510 implementation

`sh3673510.c/.h` implements:

- hardware SPI, MSB first, MODE3 (CPOL=1/CPHA=1), 1 MHz;
- register read/write protocol and CRC8 (`x^8+x^2+x+1`, initial 0);
- software reset and 12 ms warm-up margin;
- 10-series configuration (`CN=10`);
- VADC cell/temperature/current conversion;
- CADC current conversion with the 0.25 mOhm effective shunt;
- VTOP and VCHGR conversion;
- FLAG/BSTATUS acquisition;
- OV/UV hardware threshold configuration;
- charge/discharge MOS request control;
- balance-register access;
- hardware watchdog at 3.92 s;
- AFE watchdog alarm plus automatic BMS-side AFE re-initialization after repeated SPI failures.

### SPI timing note

The SH3673510 read SDO sequence is `0xFF, READ_CMD, REG_ADDR, DATA_LEN, DATA1..DATAN, CRC8`. The B85 SDK `spi_read()` sends the command first and then deliberately clocks/discards the first post-command byte. For this AFE that discarded byte is the echoed `DATA_LEN`, so the driver requests `N+1` returned bytes and treats them as `DATA1..DATAN + CRC8`.

For register writes and software reset, the AFE returns ACK/NACK while the final invalid/dummy byte is clocked. The generic B85 `spi_read()` also consumes that first post-command byte internally, so the driver does not pretend to inspect an ACK that the API does not expose. It transmits the complete `CMD/REG/DATA/CRC/dummy` or `0x0B/0xBB/0xCC/CRC/dummy` timing and validates success with a CRC-protected register readback/probe.

`MOS_EN` is enabled. Therefore the SH3673510 remains the authority for hardware protection and also provides the AFE-native opposite-current MOS reopen behavior. Software does not repeatedly force a MOS against an active AFE veto.

## AFE-independent BMS parameter model

The application layer no longer treats SH3673510 register codes as BMS parameters.

- `bms_param.c/.h` owns stable logical BMS parameters in physical units.
- `bms_afe.c/.h` is the adapter between those logical parameters and the selected AFE.
- `sh3673510.c/.h` remains vendor-specific and is not referenced by Modbus/application logic.
- AFE faults are normalized into common `BMS_AFE_FAULT_*` bits before the BMS layer consumes them; raw FLAG registers remain available only for engineering diagnostics.
- Requested and effective parameter values are stored separately. AFE-backed values are range checked and quantized according to the adapter's actual capability.
- After AFE reset/reinitialization, the common parameter DB is re-projected to the AFE instead of treating AFE registers as the source of truth.

For this board, Cell OV L3 and Cell UV L3 are already mapped through the adapter. Their ranges and 5 mV step come from the SH3673510 capability. Other current/temperature levels remain present in the common model but are not marked ACTIVE until their exact software/AFE enforcement mapping is implemented; the firmware does not invent an OCD1/OCD2/OCC/SC mapping from legacy level names.

See `BMS_COMMON_PARAMETER_PROTOCOL.md` for the PC/app-facing descriptor and value protocol.

## BLE compatibility

The branch keeps the previous BMS behavior:

- Telink SPP UUIDs are unchanged;
- phone writes the historical `SERVER2CLIENT` characteristic;
- module replies by notification on the historical `CLIENT2SERVER` characteristic;
- SPP payload is the same Modbus RTU frame used on RS485;
- Telink OTA service remains enabled;
- advertising interval: 800 ms;
- all advertising channels;
- RF power: +3 dBm;
- connection update request: 10 ms interval, latency 99, 4 s supervision timeout;
- BLE name defaults to `BT_HSD011_10S50A` and keeps the old Modbus name window at `0x0100`.

The stock sample's unconditional 60-second deep-sleep path is suppressed. BLE suspend remains enabled. A board-specific deep-sleep policy must only be enabled after the actual wake-source polarities and desired product behavior are confirmed on hardware.

## Modbus compatibility and V2.1 discovery

Slave address remains `0x01`; RTU supports functions `0x03`, `0x06`, `0x10`.

| Address | Meaning |
|---|---|
| `0x0000..0x0002` | BLE public MAC, same two-bytes-per-register packing as the previous project |
| `0x0100..` | BLE name |
| `0x1005` | SOC write compatibility |
| `0x1102` | command compatibility / project commands |
| `0x2000..0x200F` | V2.1 protocol/AFE/capability discovery |
| `0x2100..0x2140` | legacy 16-bit protection parameter window, translated into the common DB |
| `0x4000..` | per-parameter capability descriptors: logical ID, unit, range, step, enforcement, quantization, requested/effective values |
| `0x4400..` | common requested values, signed32 physical units, RW with FC10 |
| `0x4500..` | common effective values, signed32 physical units, RO |
| `0xC002..` | SN / hardware version / software version |
| `0xD000..0xD03E` | legacy `stCell_Info`-style realtime window |
| `0xD115` | runtime/AFE status |
| `0xD116` | raw SH3673510 FLAG2:FLAG1 engineering view |
| `0xD120..0xD12A` | compact realtime block (`0x4253`, version 1) |

New clients should start by reading `0x2000..0x200F`, then build their parameter UI from the descriptors instead of shipping SH3673510-specific ranges in the app. Existing clients can keep using `0x2100..0x2140`; both paths modify the same parameter DB.

Common signed32 values reject FC06 half-writes. FC10 must write complete high/low pairs. A multi-parameter write validates the whole logical block first, applies AFE projections, rolls earlier AFE writes back if a later hardware write fails, and commits the RAM DB only after the hardware phase succeeds.

Current sign in the compact signed-current register is kept compatible with the previous application protocol: **positive = charging, negative = discharging**. Internally the raw SH3673510 polarity is retained (positive CUR = discharge, negative CUR = charge).

Project commands currently added at `0x1102`:

- `0x0010`: clear current normalized AFE protection faults;
- `0x0011`: request charge + discharge MOS on;
- `0x0012`: request both MOS off.

## Intentional boundaries

The following are deliberately not guessed from the schematic or old level names:

1. Exact mapping of common charge/discharge L1/L2/L3 current parameters onto SH3673510 OCD1/OCD2/OCC/SC. The AFE's native protection remains configured, but these common fields stay `ACTIVE=0` until the product mapping is explicit.
2. Final automatic balancing start voltage/delta policy. Low-level balance control is implemented, but automatic balancing is not enabled with invented thresholds.
3. Product deep-sleep entry criteria and wake polarities. The generic Telink demo deep-sleep behavior is disabled rather than risking an unreachable board.
4. Flash persistence for writable common parameters/BLE name. The RAM parameter DB, validation, quantization and hardware transaction layer are implemented; persistence still needs a reserved flash layout that does not collide with Telink OTA/system sectors.

## Bench bring-up order

1. Power board without load/charger; verify TLSR8251 remains alive and BLE advertises `BT_HSD011_10S50A`.
2. Read `0x2000..0x200F`; confirm magic `0x424D`, protocol `0x0201`, AFE type `0x3510`, 10 cells and TS mask.
3. Read Cell OV L3 descriptor, requested and effective values; write a non-5mV-aligned common OV with FC10 and confirm conservative effective quantization/readback.
4. Read Modbus `0xD115/0xD116`; confirm AFE online/init bits and no unexpected protection flags.
5. Read `0xD000..0xD009`; compare all ten cell voltages against a DMM.
6. Read TS1/TS2/TS4 and verify room-temperature values.
7. Apply known bidirectional current and verify current magnitude and sign on both legacy and compact registers.
8. Test RS485 115200 8N1, slave 1, functions 03/06/10.
9. Test the same Modbus frames over BLE SPP.
10. Verify OTA with the existing Telink BMS updater.
11. Independently inject OV/UV/OCD/OCC/SC/temperature faults and confirm AFE MOS veto behavior before enabling higher-level automatic policies.
