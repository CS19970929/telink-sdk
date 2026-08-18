"""BLE and deterministic demo transports. The client layer never sees an AFE."""

from __future__ import annotations

import abc
import asyncio
import struct
from typing import Callable

from .protocol import Command, Decoder, FLAG_RESPONSE, Frame


BMS_SERVICE_UUID = "b1a50001-a00d-4692-9144-5e8e20689457"
BMS_RX_UUID = "b1a50001-a00d-4692-9144-5e8e20689458"
BMS_TX_UUID = "b1a50001-a00d-4692-9144-5e8e20689459"
TELINK_OTA_UUID = "00010203-0405-0607-0809-0a0b0c0d2b12"


class BmsTransport(abc.ABC):
    @abc.abstractmethod
    async def connect(self, address: str | None = None) -> None: ...

    @abc.abstractmethod
    async def disconnect(self) -> None: ...

    @abc.abstractmethod
    async def exchange(self, packet: bytes, timeout_s: float = 3.0) -> list[bytes]: ...


class BleakTransport(BmsTransport):
    """Optional production BLE transport. Requires ``pip install -r requirements.txt``."""

    def __init__(self) -> None:
        self._client = None
        self._notifications: asyncio.Queue[bytes] = asyncio.Queue()

    @staticmethod
    async def scan(timeout_s: float = 5.0) -> list[tuple[str, str]]:
        try:
            from bleak import BleakScanner
        except ModuleNotFoundError as error:
            raise RuntimeError("缺少 bleak；请安装 pc_client/requirements.txt") from error
        devices = await BleakScanner.discover(timeout=timeout_s)
        return [(device.address, device.name or "未命名 BLE 设备") for device in devices]

    async def connect(self, address: str | None = None) -> None:
        if not address:
            raise ValueError("需要 BLE 地址")
        try:
            from bleak import BleakClient
        except ModuleNotFoundError as error:
            raise RuntimeError("缺少 bleak；请安装 pc_client/requirements.txt") from error
        self._client = BleakClient(address)
        await self._client.connect()
        await self._client.start_notify(BMS_TX_UUID, self._on_notification)

    def _on_notification(self, _: int, data: bytearray) -> None:
        self._notifications.put_nowait(bytes(data))

    async def disconnect(self) -> None:
        if self._client is not None:
            if self._client.is_connected:
                await self._client.disconnect()
            self._client = None

    async def exchange(self, packet: bytes, timeout_s: float = 3.0) -> list[bytes]:
        if self._client is None or not self._client.is_connected:
            raise RuntimeError("尚未连接 BLE 设备")
        while not self._notifications.empty():
            self._notifications.get_nowait()
        await self._client.write_gatt_char(BMS_RX_UUID, packet, response=False)
        return [await asyncio.wait_for(self._notifications.get(), timeout_s)]


class DemoTransport(BmsTransport):
    """Offline protocol simulator used by UI demos and automated tests."""

    def __init__(self) -> None:
        self._decoder = Decoder()
        self._connected = False
        self.parameters = {0x0101: 4250, 0x0102: 4150, 0x0201: 20000, 0x0202: 500}

    async def connect(self, address: str | None = None) -> None:
        self._connected = True

    async def disconnect(self) -> None:
        self._connected = False

    async def exchange(self, packet: bytes, timeout_s: float = 3.0) -> list[bytes]:
        if not self._connected:
            raise RuntimeError("演示设备尚未连接")
        requests = self._decoder.feed(packet)
        if len(requests) != 1:
            raise RuntimeError("演示传输收到无效帧")
        request = requests[0]
        response = self._respond(request).encode()
        return [response[:17], response[17:]]

    def _respond(self, request: Frame) -> Frame:
        command = Command(request.command)
        if command is Command.GET_DEVICE_INFO:
            return Frame(request.sequence, command, bytes([0, 2, 0, 0x51, 1, 20, 4, 0]) + struct.pack("<I", 0), FLAG_RESPONSE)
        if command is Command.GET_REALTIME:
            cells = [3650 + index for index in range(20)]
            temperatures = [250, 252, 248, 255]
            payload = struct.pack("<IIIiiHHBB", 0x0F, 123456, sum(cells), 1200, sum(cells) * 12 // 10, 500, 1000, len(cells), len(temperatures))
            payload += struct.pack("<" + "H" * len(cells), *cells)
            payload += struct.pack("<" + "h" * len(temperatures), *temperatures)
            payload += struct.pack("<IIII", 0, 0, 0, 0)
            return Frame(request.sequence, command, payload + bytes([0x03]), FLAG_RESPONSE)
        if command is Command.GET_PARAMETERS:
            payload = bytearray([len(self.parameters)])
            for parameter_id, value in self.parameters.items():
                payload += struct.pack("<HBi", parameter_id, 0, value)
            return Frame(request.sequence, command, bytes(payload), FLAG_RESPONSE)
        if command is Command.SET_PARAMETERS:
            for offset in range(0, len(request.payload), 6):
                parameter_id, value = struct.unpack_from("<Hi", request.payload, offset)
                self.parameters[parameter_id] = value
            return Frame(request.sequence, command, bytes([len(request.payload) // 6]), FLAG_RESPONSE)
        if command is Command.GET_PARAMETER_SCHEMA:
            payload = bytearray([len(self.parameters)])
            for parameter_id, value in self.parameters.items():
                payload += struct.pack("<HBBiii", parameter_id, 0, 7, 0, 300000, value)
            return Frame(request.sequence, command, bytes(payload), FLAG_RESPONSE)
        if command is Command.GET_FAULTS:
            return Frame(request.sequence, command, bytes(12), FLAG_RESPONSE)
        if command is Command.GET_EVENT_LOG:
            return Frame(request.sequence, command, b"\x00", FLAG_RESPONSE)
        if command is Command.OTA_INFO:
            return Frame(request.sequence, command, bytes([1, 0, 15, 0]), FLAG_RESPONSE)
        return Frame(request.sequence, command, b"\x04", FLAG_RESPONSE | (1 << 2))
