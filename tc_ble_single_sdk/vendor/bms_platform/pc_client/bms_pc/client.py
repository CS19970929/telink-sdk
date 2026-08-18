"""High-level BMSLink client. It owns BMS semantics, not BLE or AFE details."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from pathlib import Path
from typing import Callable

from .protocol import Command, Decoder, FLAG_ERROR, FLAG_RESPONSE, Frame
from .transport import BmsTransport


@dataclass(frozen=True)
class DeviceInfo:
    firmware: tuple[int, int, int]
    mcu: int
    afe_kind: int
    cell_count: int
    temperature_count: int
    power_topology: int
    capabilities: int


@dataclass(frozen=True)
class Realtime:
    valid_flags: int
    timestamp_ms: int
    pack_voltage_mv: int
    current_ma: int
    power_mw: int
    soc_permil: int
    soh_permil: int
    cells_mv: tuple[int, ...]
    temperatures_decic: tuple[int, ...]
    cell_min_mv: int
    cell_max_mv: int
    cell_delta_mv: int
    balance_mask: int
    alarm_flags: int
    protection_flags: int
    fault_flags: int
    state_flags: int


@dataclass(frozen=True)
class Parameter:
    parameter_id: int
    type: int
    value: int


@dataclass(frozen=True)
class ParameterSchema:
    parameter_id: int
    type: int
    flags: int
    minimum: int
    maximum: int
    default: int


@dataclass(frozen=True)
class Event:
    timestamp_ms: int
    type: int
    severity: int
    before: int
    after: int


class BmsProtocolError(RuntimeError):
    pass


class BmsClient:
    def __init__(self, transport: BmsTransport) -> None:
        self.transport = transport
        self._sequence = 1
        self._decoder = Decoder()

    async def connect(self, address: str | None = None) -> None:
        await self.transport.connect(address)

    async def disconnect(self) -> None:
        await self.transport.disconnect()

    async def request(self, command: Command, payload: bytes = b"") -> Frame:
        sequence = self._sequence
        self._sequence = 1 if sequence == 0xFFFF else sequence + 1
        packet = Frame(sequence=sequence, command=command, payload=payload).encode()
        fragments = await self.transport.exchange(packet)
        for fragment in fragments:
            for frame in self._decoder.feed(fragment):
                if frame.sequence != sequence or frame.command != command:
                    continue
                if not frame.flags & FLAG_RESPONSE:
                    raise BmsProtocolError("设备返回的不是响应帧")
                if frame.flags & FLAG_ERROR:
                    error = frame.payload[0] if frame.payload else -1
                    raise BmsProtocolError(f"设备拒绝命令 0x{command:02X}，错误码 {error}")
                return frame
        raise BmsProtocolError("未收到完整 BMSLink 响应")

    async def device_info(self) -> DeviceInfo:
        payload = (await self.request(Command.GET_DEVICE_INFO)).payload
        if len(payload) != 12:
            raise BmsProtocolError("设备信息长度错误")
        return DeviceInfo((payload[0], payload[1], payload[2]), payload[3], payload[4],
                          payload[5], payload[6], payload[7], struct.unpack_from("<I", payload, 8)[0])

    async def realtime(self) -> Realtime:
        payload = (await self.request(Command.GET_REALTIME)).payload
        if len(payload) < 26:
            raise BmsProtocolError("实时数据长度错误")
        valid, timestamp, pack, current, power, soc, soh, cell_count, temp_count = struct.unpack_from("<IIIiiHHBB", payload)
        required = 26 + cell_count * 2 + temp_count * 2 + 17
        if len(payload) != required:
            raise BmsProtocolError("实时数据数组长度错误")
        offset = 26
        cells = struct.unpack_from("<" + "H" * cell_count, payload, offset)
        offset += cell_count * 2
        temps = struct.unpack_from("<" + "h" * temp_count, payload, offset)
        offset += temp_count * 2
        balance, alarms, protections, faults = struct.unpack_from("<IIII", payload, offset)
        offset += 16
        cell_min = min(cells, default=0)
        cell_max = max(cells, default=0)
        delta = cell_max - cell_min
        return Realtime(valid, timestamp, pack, current, power, soc, soh, cells, temps,
                        cell_min, cell_max, delta, balance, alarms, protections, faults,
                        payload[offset])

    async def parameters(self, start_id: int = 0, count: int = 18) -> list[Parameter]:
        payload = struct.pack("<HB", start_id, count)
        response = (await self.request(Command.GET_PARAMETERS, payload)).payload
        if not response or len(response) != 1 + response[0] * 7:
            raise BmsProtocolError("参数响应长度错误")
        return [Parameter(*struct.unpack_from("<HBi", response, 1 + index * 7))
                for index in range(response[0])]

    async def all_parameters(self) -> list[Parameter]:
        result: list[Parameter] = []
        start_id = 0
        while True:
            page = await self.parameters(start_id)
            result.extend(page)
            if len(page) < 18:
                return result
            start_id = page[-1].parameter_id + 1

    async def schema(self, start_id: int = 0, count: int = 7) -> list[ParameterSchema]:
        response = (await self.request(Command.GET_PARAMETER_SCHEMA, struct.pack("<HB", start_id, count))).payload
        if not response or len(response) != 1 + response[0] * 16:
            raise BmsProtocolError("Schema 响应长度错误")
        return [ParameterSchema(*struct.unpack_from("<HBBiii", response, 1 + index * 16))
                for index in range(response[0])]

    async def all_schema(self) -> list[ParameterSchema]:
        result: list[ParameterSchema] = []
        start_id = 0
        while True:
            page = await self.schema(start_id)
            result.extend(page)
            if len(page) < 7:
                return result
            start_id = page[-1].parameter_id + 1

    async def set_parameters(self, values: dict[int, int]) -> None:
        if not values or len(values) > 21:
            raise ValueError("一次必须写入 1–21 个参数")
        payload = b"".join(struct.pack("<Hi", parameter_id, value)
                           for parameter_id, value in values.items())
        response = (await self.request(Command.SET_PARAMETERS, payload)).payload
        if response != bytes([len(values)]):
            raise BmsProtocolError("参数写入确认错误")

    async def set_soc(self, soc_permil: int) -> None:
        if not 0 <= soc_permil <= 1000:
            raise ValueError("SOC 范围为 0–1000‰")
        await self.request(Command.CONTROL, bytes([1]) + struct.pack("<H", soc_permil))

    async def faults(self) -> tuple[int, int, int]:
        payload = (await self.request(Command.GET_FAULTS)).payload
        if len(payload) != 12:
            raise BmsProtocolError("故障响应长度错误")
        return struct.unpack("<III", payload)

    async def events(self, start: int = 0, count: int = 9) -> list[Event]:
        payload = (await self.request(Command.GET_EVENT_LOG, bytes([start, count]))).payload
        if not payload or len(payload) != 1 + payload[0] * 14:
            raise BmsProtocolError("日志响应长度错误")
        return [Event(*struct.unpack_from("<IBBII", payload, 1 + index * 14))
                for index in range(payload[0])]

    async def ota_info(self) -> tuple[bool, bool, int]:
        payload = (await self.request(Command.OTA_INFO)).payload
        if len(payload) != 4:
            raise BmsProtocolError("OTA 信息长度错误")
        return bool(payload[0]), bool(payload[1]), payload[2]

    async def ota_update(self, image_path: str | Path,
                         progress: Callable[[int, int], None] | None = None):
        """Perform a checked Telink OTA transfer after the firmware permits it."""
        from .ota import OtaError, load_firmware_image, transfer_telink_ota
        from .transport import BleakTransport

        available, layout_approved, timeout_seconds = await self.ota_info()
        if not available or not layout_approved:
            raise OtaError("设备未批准 OTA Flash 布局，已拒绝写 Flash")
        if not isinstance(self.transport, BleakTransport):
            raise OtaError("OTA 仅支持已连接的真实 BleakTransport，不支持演示设备")
        return await transfer_telink_ota(self.transport, load_firmware_image(image_path),
                                         timeout_seconds, progress)
