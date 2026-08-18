"""Telink legacy OTA packet construction and checked BLE transfer flow."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Callable

from .transport import BleakTransport


TELINK_OTA_START = 0xFF01
TELINK_OTA_END = 0xFF02
TELINK_OTA_RESULT = 0xFF06
TELINK_OTA_BLOCK_BYTES = 16
TELINK_OTA_IMAGE_SIZE_OFFSET = 0x18
TELINK_OTA_MAX_BLOCKS = 0x10000

OTA_RESULT_TEXT = {
    0: "成功",
    1: "数据包序号错误",
    2: "数据包无效",
    3: "数据包 CRC 错误",
    4: "写 Flash 失败",
    5: "镜像数据不完整",
    6: "OTA 流程错误",
    7: "固件校验失败",
    8: "版本比较失败",
    9: "OTA PDU 长度错误",
    10: "不是 Telink SDK 镜像",
    11: "固件大小错误",
    12: "数据包间隔超时",
    13: "OTA 总流程超时",
    14: "连接中断导致失败",
    15: "MCU 不支持此 OTA 模式",
    16: "OTA 内部逻辑错误",
}


class OtaError(RuntimeError):
    """The OTA server refused the image or the transfer protocol failed."""


@dataclass(frozen=True)
class FirmwareImage:
    """A Telink SDK image trimmed to its firmware-size declaration."""

    data: bytes
    declared_size: int

    @property
    def block_count(self) -> int:
        return (self.declared_size + TELINK_OTA_BLOCK_BYTES - 1) // TELINK_OTA_BLOCK_BYTES


@dataclass(frozen=True)
class OtaOutcome:
    image_size: int
    block_count: int


def telink_ota_crc16(data: bytes) -> int:
    """Return the SDK legacy-OTA CRC16 (Modbus/IBM, init ``0xFFFF``)."""
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if (crc & 1) else 0)
    return crc


def load_firmware_image(path: str | Path) -> FirmwareImage:
    """Validate a checked Telink ``.bin`` and retain exactly its declared image."""
    source = Path(path)
    if source.suffix.lower() != ".bin":
        raise OtaError("OTA 镜像必须是由构建流程生成的 .bin 文件")
    try:
        raw = source.read_bytes()
    except OSError as error:
        raise OtaError(f"无法读取 OTA 镜像：{source}") from error
    if len(raw) < TELINK_OTA_IMAGE_SIZE_OFFSET + 4:
        raise OtaError("OTA 镜像过短，缺少 Telink 固件大小字段")
    declared_size = struct.unpack_from("<I", raw, TELINK_OTA_IMAGE_SIZE_OFFSET)[0]
    if declared_size == 0:
        raise OtaError("OTA 镜像未声明有效固件大小；请使用 SDK 检查后的镜像")
    if declared_size > len(raw):
        raise OtaError("OTA 镜像大小字段超过文件长度")
    if (declared_size + TELINK_OTA_BLOCK_BYTES - 1) // TELINK_OTA_BLOCK_BYTES > TELINK_OTA_MAX_BLOCKS:
        raise OtaError("OTA 镜像超过 Telink 16 位块序号可表示的范围")
    return FirmwareImage(raw[:declared_size], declared_size)


def make_ota_data_packet(image: FirmwareImage, block_index: int) -> bytes:
    """Build one 20-byte OTA write: LE block index, 16 data bytes, LE CRC16."""
    if not 0 <= block_index < image.block_count:
        raise ValueError("OTA 数据块序号超出镜像范围")
    offset = block_index * TELINK_OTA_BLOCK_BYTES
    data = image.data[offset:offset + TELINK_OTA_BLOCK_BYTES].ljust(TELINK_OTA_BLOCK_BYTES, b"\xff")
    without_crc = struct.pack("<H", block_index) + data
    return without_crc + struct.pack("<H", telink_ota_crc16(without_crc))


def make_ota_end_packet(image: FirmwareImage) -> bytes:
    """Build the checked legacy end command for the image's final block."""
    if image.block_count == 0:
        raise ValueError("空 OTA 镜像没有结束数据块")
    final_index = image.block_count - 1
    return struct.pack("<HHH", TELINK_OTA_END, final_index, final_index ^ 0xFFFF)


async def transfer_telink_ota(
    transport: BleakTransport,
    image: FirmwareImage,
    timeout_seconds: int,
    progress: Callable[[int, int], None] | None = None,
) -> OtaOutcome:
    """Transfer an image and wait for the server's explicit ``CMD_OTA_RESULT``."""
    if not 5 <= timeout_seconds <= 1000:
        raise OtaError("设备给出的 OTA 超时值不在 SDK 允许的 5–1000 秒范围内")
    transport.clear_ota_notifications()
    await transport.ota_write(struct.pack("<H", TELINK_OTA_START))
    await asyncio.sleep(0.05)
    for block_index in range(image.block_count):
        await transport.ota_write(make_ota_data_packet(image, block_index))
        if progress is not None:
            progress(block_index + 1, image.block_count)
        if (block_index & 0x1F) == 0x1F:
            await asyncio.sleep(0)
    await transport.ota_write(make_ota_end_packet(image))
    result = await transport.wait_ota_result(float(timeout_seconds))
    if result != 0:
        raise OtaError("设备拒绝 OTA：" + OTA_RESULT_TEXT.get(result, f"未知结果码 {result}"))
    return OtaOutcome(image.declared_size, image.block_count)
