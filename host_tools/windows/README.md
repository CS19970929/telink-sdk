# Telink BMS Host — Windows

Windows host application matched to the `telink-sdk` BMS firmware. The first version targets Windows 10/11 and keeps the protocol core independent of Windows so it can be reused by a later Android/iOS client.

## Implemented

- Serial Modbus RTU, 115200 8N1 by default.
- Automatic Modbus retry. This intentionally tolerates the firmware policy where the first serial request after low-power entry may only wake the MCU and may be lost.
- Bluetooth LE scan/connect using the BMS Nordic UART Service (NUS-compatible) UUIDs.
- The same Modbus RTU payload is carried over Serial or BLE; BMS pages do not care which transport is selected.
- Realtime pack/cell/current/SOC/SOH/temperature display.
- AFE/protection/MOS/serial-PM diagnostics.
- Read/write all 13 protection parameter groups through the existing legacy `0x2100` window, with user-facing current and temperature units.
- Product serial/hardware/software identity and BLE name read/write.
- Raw FC03/FC06 register console.
- MOS ON/OFF commands.
- Telink BLE OTA using the firmware's existing OTA GATT service and the conservative 16-byte legacy OTA PDU format.

## Current OTA scope

The current MCU firmware exposes OTA through the Telink BLE OTA GATT service. It does **not** expose a serial-IAP/serial-OTA protocol, so this host enables OTA only for BLE. Serial OTA should be added only after a matching firmware-side serial IAP contract exists.

OTA uses:

- Service UUID: `00010203-0405-0607-0809-0A0B0C0D1912`
- Characteristic UUID: `00010203-0405-0607-0809-0A0B0C0D2B12`
- `CMD_OTA_START = 0xFF01`
- Data packet: `adr_index LE16 + 16 firmware bytes + Telink CRC16 LE16`
- `CMD_OTA_END = 0xFF02 + last_index + (last_index XOR 0xFFFF)`

The host uses 16-byte OTA data PDUs deliberately. They fit the default 20-byte ATT value and avoid depending on negotiated MTU/DLE or the B85 BigPDU limit.

## Firmware BLE data transport

- NUS service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- BMS request/write: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- BMS response/notify: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

The firmware may split a Modbus response into multiple 20-byte notifications. `BleBmsTransport` reassembles the stream by Modbus function/length/CRC before returning a response to the protocol layer.

## Requirements

- Windows 10/11.
- .NET 7 SDK or later for source builds. The project currently targets `net7.0` / `net7.0-windows10.0.19041.0` so an existing .NET SDK 7.0.400 installation can build it directly.
- Bluetooth LE adapter for BLE transport.
- 3.3 V USB-TTL or the actual serial interface for direct UART testing.

## Build

From PowerShell:

```powershell
cd host_tools\windows
.\build-win.ps1
```

Or:

```powershell
dotnet build .\BmsHost.sln -c Release
```

Run from source:

```powershell
dotnet run --project .\src\BmsHost.Win\BmsHost.Win.csproj -c Release
```

Publish a standalone `win-x64` application:

```powershell
.\publish-win-x64.ps1
```

Output:

```text
publish\win-x64\BmsHost.Win.exe
```

## Recommended bench sequence

1. Connect over Serial and verify 200–1000 ms auto-polling is stable.
2. Disable auto refresh, wait more than 3 seconds, then press `Refresh now`; the built-in Modbus retry should wake the sleeping UART transport and complete without manual intervention.
3. Connect over BLE and verify the same realtime/parameter pages.
4. Verify BLE name read/write.
5. OTA with a known-good post-checked `825x_ble_sample.bin`; keep BMS power stable. After OTA end, reconnect and verify identity/behavior.
6. Only after successful OTA bench validation use the OTA function on field units.

## Cross-platform direction

`src/BmsHost.Core` contains transport-neutral Modbus, BMS register mapping, parameter encoding, models, and Telink OTA packet generation. A later Android/iOS app can reuse this project and replace only the Windows transport/UI layer (for example with .NET MAUI/native BLE transports). Generic serial is intentionally treated as a Windows/desktop transport; mobile clients are expected to use BLE unless specific USB/serial hardware support is added.
