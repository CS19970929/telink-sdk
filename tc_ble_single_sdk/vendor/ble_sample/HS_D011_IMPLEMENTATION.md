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

For register writes and software reset, the AFE returns ACK/NACK while the final invalid/dummy byte is clocked. The generic B85 `spi_read()` also consumes that first post-command byte internally, so the driver does not pretend to inspect an ACK that the API does not expose. It transmits the complete timing and validates success with a CRC-protected register readback/probe.

`MOS_EN` is enabled. SH3673510 remains the hardware authority: software may request MOS state, but active AFE protection is not bypassed.

## AFE-independent BMS parameter model

The application layer no longer treats SH3673510 register codes as BMS parameters.

- `bms_param.c/.h` owns stable logical BMS parameters in physical units.
- `bms_afe.c/.h` is the adapter between common logical parameters and the selected AFE.
- `sh3673510.c/.h` remains vendor-specific and is not referenced by Modbus/application policy.
- AFE faults are normalized into common `BMS_AFE_FAULT_*` bits; raw FLAG registers remain engineering diagnostics only.
- Requested and effective parameter values are separate. AFE-backed values are range checked and quantized according to the adapter's real capability.
- After AFE reset/reinitialization, the common parameter DB is re-projected into the AFE.

For this board, Cell OV L3 and Cell UV L3 have direct AFE projection and report `HYBRID` enforcement. Other resolved groups are enforced by the software protection engine. Bus/Pack OV/UV remain recognized but inactive because their legacy product semantics are not yet unambiguous.

See `BMS_COMMON_PARAMETER_PROTOCOL.md` for the PC/app-facing protocol.

## Software protection engine

`bms_protect.c/.h` evaluates the common parameter DB from normalized AFE samples.

- L1: alarm/status level only.
- L2/L3: protection levels that veto the corresponding MOS direction.
- Cell OV / charge OC / charge OT / charge UT -> charge veto.
- Cell UV / discharge OC / discharge OT / discharge UT / low SOC -> discharge veto.
- MOS OT -> charge + discharge veto.
- Cell delta is evaluated/reported but does not directly veto MOS at present.
- Per-group recovery and delay use the same common parameters exposed to the app.

The project is a common-port design. `BMS_PROTECT_OPPOSITE_REOPEN_ENABLE=1` implements the existing product requirement that a software protection which blocks one direction must not unnecessarily block opposite-direction current. Hardware AFE protection remains final authority, so the software does not defeat an SH3673510 hardware veto.

## Parameter persistence

Common requested parameters are persisted in two 4 KiB slots:

- slot A: `0x72000`;
- slot B: `0x73000`.

The current target is TLSR8251F512. Records contain magic/version/schema/count/sequence, all requested signed32 values and CRC32. Writes alternate A/B slots; the new slot is erased, written and read back before becoming the active record. Startup selects the newest CRC-valid slot.

Parameter writes are debounced before flash commit to avoid erasing a sector for every slider/write operation. A restored record is accepted only if every value still passes the current schema/AFE limits; otherwise the complete record is rejected and defaults remain active.

The storage code explicitly rejects a non-512-KiB flash capacity instead of writing the configured addresses on an unknown device.

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

The stock sample's unconditional 60-second deep-sleep path is suppressed. BLE suspend remains enabled. Board-specific deep-sleep policy remains intentionally disabled until wake-source polarity/product behavior is bench-confirmed.

## Modbus compatibility and V2.1 discovery

Slave address remains `0x01`; RTU supports functions `0x03`, `0x06`, `0x10`.

| Address | Meaning |
|---|---|
| `0x0000..0x0002` | BLE public MAC |
| `0x0100..` | BLE name |
| `0x1005` | SOC write compatibility |
| `0x1102` | project commands |
| `0x2000..0x200F` | V2.1 protocol/AFE/capability discovery |
| `0x2100..0x2140` | legacy 16-bit protection parameter window, translated into the common DB |
| `0x4000..` | per-parameter capability descriptors |
| `0x4400..` | common requested values, signed32 physical units, RW with FC10 |
| `0x4500..` | common effective values, signed32 physical units, RO |
| `0xC002..` | SN / hardware version / software version |
| `0xD000..0xD03E` | legacy realtime window |
| `0xD115` | runtime/AFE status |
| `0xD116` | raw SH3673510 FLAG2:FLAG1 engineering view |
| `0xD120..0xD12A` | compact realtime block (`0x4253`, version 1) |
| `0xD130..0xD13A` | common protection/persistence diagnostics (`0x5052`, version 1) |

The `0xD130` block exposes L1/L2/L3/common-active bitmaps, MOS user request/veto/effective state, last MOS command error, normalized AFE faults and parameter-persistence status. Group bitmap bit positions are identical to the stable logical parameter group numbers, so an app can decode protection state without knowing SH3673510 FLAG bits.

New clients should start by reading `0x2000..0x200F`, build the parameter UI from descriptors, use `0x4400/0x4500` for values, and use `0xD130..0xD13A` for protection diagnostics. Existing clients can keep using `0x2100..0x2140`; both paths modify the same parameter DB.

Common signed32 values reject FC06 half-writes. FC10 must write complete high/low pairs. A multi-parameter write validates the whole logical block first, applies AFE projections, rolls earlier AFE writes back if a later hardware write fails, and commits the RAM DB only after the hardware phase succeeds.

Current sign in the compact signed-current register is kept compatible with the previous application protocol: **positive = charging, negative = discharging**. Internally the SH3673510 sample convention is retained: positive current = discharge, negative current = charge.

Project commands at `0x1102`:

- `0x0010`: clear normalized AFE protection faults;
- `0x0011`: request charge + discharge MOS on;
- `0x0012`: request both MOS off.

## Intentional boundaries

The following are still deliberately not guessed:

1. Mapping of common charge/discharge L1/L2/L3 current parameters onto SH3673510 native OCD1/OCD2/OCC/SC stages. The common levels are already enforced in software; native AFE current-stage mapping remains a separate product policy decision.
2. Final automatic balancing start voltage/delta policy. Low-level balance control is implemented, but automatic balancing is not enabled with invented thresholds.
3. Final deep-sleep entry criteria and wake polarities.
4. BLE-name persistence is still separate from the new common protection-parameter persistence path.

## Bench bring-up order

1. Power board without load/charger; verify TLSR8251 remains alive and BLE advertises `BT_HSD011_10S50A`.
2. Read `0x2000..0x200F`; confirm magic `0x424D`, protocol `0x0201`, AFE type `0x3510`, 10 cells and TS mask.
3. Read Cell OV L3 descriptor, requested and effective values; write a non-5mV-aligned common OV with FC10 and confirm conservative effective quantization/readback.
4. Read `0xD130..0xD13A`; confirm magic/version, no unexpected software/AFE protection and persistence state.
5. Change one protection parameter, wait for the debounce save, power-cycle, and verify the requested value is restored and `0xD13A` reports a valid record.
6. Read Modbus `0xD115/0xD116`; confirm AFE online/init bits and no unexpected raw protection flags.
7. Read `0xD000..0xD009`; compare all ten cell voltages against a DMM.
8. Read TS1/TS2/TS4 and verify room-temperature values.
9. Apply known bidirectional current and verify current magnitude/sign on legacy and compact registers.
10. Inject software L1/L2/L3 threshold crossings and confirm the `0xD132..0xD136` state transition and MOS policy.
11. Test RS485 115200 8N1, slave 1, functions 03/06/10.
12. Test the same Modbus frames over BLE SPP.
13. Verify OTA with the existing Telink BMS updater.
14. Independently inject OV/UV/OCD/OCC/SC/temperature hardware faults and confirm SH3673510 remains authoritative.
