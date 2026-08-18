"""BMSLink v1 codec shared by the PC GUI, CLI and test transport."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct


SOF = b"\xb5\x4d"
VERSION = 1
MAX_PAYLOAD = 128
FLAG_RESPONSE = 1 << 0
FLAG_EVENT = 1 << 1
FLAG_ERROR = 1 << 2


class Command(IntEnum):
    GET_DEVICE_INFO = 0x01
    GET_REALTIME = 0x02
    GET_PARAMETERS = 0x10
    SET_PARAMETERS = 0x11
    GET_PARAMETER_SCHEMA = 0x12
    GET_BLE_NAME = 0x13
    SET_BLE_NAME = 0x14
    CONTROL = 0x20
    GET_FAULTS = 0x30
    GET_EVENT_LOG = 0x31
    OTA_INFO = 0x40


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass(frozen=True)
class Frame:
    sequence: int
    command: int
    payload: bytes = b""
    flags: int = 0

    def encode(self) -> bytes:
        if len(self.payload) > MAX_PAYLOAD:
            raise ValueError("BMSLink payload exceeds 128 bytes")
        header = SOF + struct.pack("<BBHBH", VERSION, self.flags, self.sequence,
                                   int(self.command), len(self.payload))
        return header + self.payload + struct.pack("<H", crc16_ccitt(header + self.payload))


class Decoder:
    """Streaming decoder: accepts arbitrary BLE notification/write fragments."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        self._buffer.extend(data)
        frames: list[Frame] = []
        while True:
            start = self._buffer.find(SOF)
            if start < 0:
                self._buffer[:] = self._buffer[-1:]
                return frames
            if start:
                del self._buffer[:start]
            if len(self._buffer) < 9:
                return frames
            version, flags, sequence, command, length = struct.unpack_from("<BBHBH", self._buffer, 2)
            if version != VERSION or length > MAX_PAYLOAD:
                del self._buffer[0]
                continue
            frame_length = 11 + length
            if len(self._buffer) < frame_length:
                return frames
            received_crc = struct.unpack_from("<H", self._buffer, 9 + length)[0]
            body = bytes(self._buffer[:9 + length])
            del self._buffer[:frame_length]
            if received_crc == crc16_ccitt(body):
                frames.append(Frame(sequence=sequence, command=command,
                                    payload=body[9:], flags=flags))
