# Tested Baselines

This file records hardware-tested firmware baselines for regression reference.

## hs-d011-ble-mock-baseline-v1

- Baseline branch: `baseline/hs-d011-ble-mock-v1`
- Tested commit: `3694df69910d8483ed94f49d3daf612489f55533`
- Product branch at test time: `hs-d011-10s50a-sh3673510`
- MCU: TLSR8251F512
- Test hardware: existing TLSR8251 board; target HS-D011 PCB not yet available
- AFE mode: mock/simulation; real AFE access disabled
- BLE device name: `BT_d011_default`

### Verified

- CLI firmware build succeeds without Telink IDE UI
- TLSR8251 startup/SRAM profile uses `MCU_STARTUP_8251`
- Linker SRAM assertions pass
- Telink `tl_check_fw2.exe` firmware post-check passes
- Firmware manifest verification passes
- BLE advertising works
- BLE device name is correct
- BLE connection is stable
- Nordic UART compatible SPP service is discovered
- SPP service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- Request/write characteristic UUID: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- Response/notify characteristic UUID: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- Modbus-over-BLE request/response communication works
- Mock BMS data can be read by the existing upper-computer application

### Not yet verified on target hardware

- SH3673510 communication and initialization
- Real cell-voltage/current/temperature acquisition
- MOS control
- Hardware/software protection coordination
- RS485 physical communication
- OTA upgrade end-to-end
- Target-board low-power current
- SOC accuracy across sleep/wake cycles

### Regression rule

Changes to BLE/GATT/SPP/Modbus transport should be compared against this baseline. Do not intentionally change the UUIDs, request/notify direction, or packet transport behavior without an explicit protocol-version change and upper-computer compatibility plan.

When a later stage is validated on real hardware, add a new baseline instead of moving or rewriting this historical baseline.
