# Multi-AFE integration and SH367309 real-data bring-up

## Current profile

Current real-board target:

- board: `legacy-309`
- AFE: `sh367309`
- AFE mode: REAL
- cells: 10S
- I2C: C0/C1, address `0x34`, 100 kHz
- shunt: 1 mOhm effective
- UART Modbus: PC2/PC3, 115200 8N1, slave `0x01`, no DE
- BLE SPP Modbus: enabled
- SH367309 runtime MOS control: enabled
- SH367309 MTP/protection-image rewrite: still disabled

Build explicitly with:

```powershell
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
python bms_tools/bms.py map
python bms_tools/bms.py verify
```

## Proven SH367309 data path retained

The new driver keeps the verified legacy wire protocol/conversions while removing application coupling:

- CRC-8 polynomial `0x07`
- RAM telemetry `0x40..0x71`
- CELL conversion `raw * 5 / 32` mV
- CADC current at `0x6E`
- CADC bit15: 1=discharge, 0=charge
- common internal current sign: +discharge / -charge
- TEMP1..TEMP3 decoded from `0x46/0x48/0x4A`
- BSTATUS/BFLAG normalized by the AFE adapter

## MOS control phase

The first telemetry-only phase has passed on the real TLSR8251 + SH367309 board, so runtime MOS control is now enabled:

```c
BMS_SH309_RESET_ON_INIT       0
BMS_SH309_MOS_CONTROL_ENABLE  1
```

The firmware still does **not** rewrite the SH367309 MTP parameter image at startup.

The legacy D3PRO hardware behavior is preserved:

- `PB6 = AFE_CTL`
- `PA1 = MCC_C`
- both pins start LOW
- CHGMOS/DSGMOS/CADCON are changed with a CONF read-modify-write
- CONF write is read back and verified
- `MCC_C` follows the charge MOS request
- `AFE_CTL` is asserted only after the CONF write succeeds
- any CONF read/write/verify failure forces the external gate controls LOW

MOS enabling is intentionally deferred until the first complete AFE sample has been evaluated by common software protection. Under normal voltage/current/temperature conditions the default request is CHG=ON, DSG=ON.

The production SOC estimator is not implemented yet, therefore SOC-low protection is temporarily disabled. An uninitialized SOC value must not falsely close the discharge MOS. Re-enable SOC-low protection only together with a real SOC-validity model.

## Build serial / product identity

Every command-line build now derives the Modbus production serial from the selected AFE and the local build date:

```text
--afe sh367309   -> SH367309-YYYYMMDD
--afe sh3673510  -> SH3673510-YYYYMMDD
--afe mock       -> MOCK-YYYYMMDD
```

Example on 2026-08-28:

```text
SH367309-20260828
SH3673510-20260828
MOCK-20260828
```

The build log prints the resulting serial. `modbus_rtu.o` is forced to rebuild on every incremental build so the date cannot remain stale across days.

Product-identification registers remain:

- serial: `0xC002`, 16 registers / 32 ASCII bytes
- hardware version follows the board profile (`LEGACY-309` or `HS-D011-V1`)
- software version remains `V1.1.0`

## Real-board validation

Useful telemetry registers:

- `0xD000..0xD009`: Cell1..Cell10, mV
- `0xD020`: max cell
- `0xD021`: min cell
- `0xD024`: cell delta
- `0xD025`: pack voltage / 10
- `0xD026`, `0xD027`: battery temperatures
- `0xD032`: charge current, 0.1 A
- `0xD033`: discharge current, 0.1 A
- `0xD130..`: common protection/MOS diagnostic window

For the MOS pass verify:

1. Build `legacy-309 + sh367309` and confirm the build log prints `SH367309-YYYYMMDD`.
2. Power the board under current-limited conditions.
3. Confirm Cell/Temp/CADC data remain correct before checking MOS state.
4. Under normal conditions verify both CHG and DSG paths turn on after the first valid sample/protection evaluation.
5. Read the protection diagnostics and confirm `last_mos_error == 0` and effective CHG/DSG requests are ON.
6. Read production serial from `0xC002` and verify it matches `SH367309-YYYYMMDD`.
7. Test Modbus command `0x1102 = 0x0012` to request both MOS OFF, then `0x0011` to request both ON.
8. Do not perform destructive over-current/short-circuit tests until the AFE hardware-protection/MTP mapping phase is complete and reviewed.

## UART note

PC2/PC3 UART Modbus is compiled and enabled on the legacy 309 profile. Low-power asynchronous UART wake without losing the first byte is still a separate release acceptance item; keep the device active during the first UART validation.

## Remaining SH367309 work

After MOS control passes, continue with native protection/MTP parameter projection, VPRO/program/readback safety, verified fault-clear semantics, UART/low-power arbitration, then the production SOC/SOH subsystem.
