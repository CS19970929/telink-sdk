# Firmware contract used by the Windows host

This file records only the pieces the host currently consumes.

## Modbus transport

Slave address: `0x01`.

Serial: `115200 8N1`, direct UART on the current legacy-309 validation board. The firmware can enter serial low power after inactivity; the first request after sleep is allowed to be lost, so the host retries requests automatically.

BLE: Modbus RTU frames are written unchanged to NUS `...0002`; responses are sent unchanged on NUS `...0003` notifications and may be split into 20-byte chunks.

## Main register blocks

- `0x0100..0x010B`: Bluetooth name.
- `0x2100..0x2140`: 13 protection groups × 5 legacy fields.
- `0xC002..`: product serial/hardware/software strings.
- `0xD000..`: legacy realtime/cells.
- `0xD115..0xD116`: AFE status/flags.
- `0xD120..0xD12A`: compact realtime block.
- `0xD130..0xD13A`: protection/MOS status.
- `0xD140..0xD149`: serial PM diagnostics.
- `0x1102`: command register (`0x0011` MOS ON, `0x0012` MOS OFF).

## Protection parameter legacy encoding

Each group has L1, L2, L3, recovery, delay.

- Voltage, cell delta, SOC: direct integer engineering value.
- Charge/discharge OC: raw unit is `0.1 A`; host displays A.
- Temperature: raw is `(°C + 40) * 10`; host displays °C.
- Delay: ms.

Group order: Cell OV, Cell UV, Bus OV, Bus UV, Charge OC, Discharge OC, Charge OT, Charge UT, Discharge OT, Discharge UT, MOS OT, Cell Delta, SOC Low.

## OTA

The firmware has `BLE_OTA_SERVER_ENABLE=1` and exposes Telink OTA through GATT. The Windows client uses the legacy 16-byte PDU mode for the initial implementation because it is the most conservative B85-compatible form.

The firmware BIN declares firmware size at offset `0x18..0x1B` (little-endian). The host uses that size when valid and falls back to the file length otherwise. The final data block is padded with `0xFF` to 16 bytes before CRC.

Telink CRC16: init `0xFFFF`, reflected polynomial `0xA001`, computed over `adr_index + data`.
