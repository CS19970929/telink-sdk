"""Headless PC-client entry point, useful for BLE lab and automation use."""

from __future__ import annotations

import argparse
import asyncio
from pathlib import Path

from .client import BmsClient
from .transport import BleakTransport, DemoTransport


async def main_async() -> None:
    parser = argparse.ArgumentParser(description="Telink BMSLink PC 客户端")
    parser.add_argument("--demo", action="store_true", help="使用离线演示设备")
    parser.add_argument("--address", help="目标 BLE 地址")
    parser.add_argument("--image", type=Path, help="SDK 构建且检查过的 Telink .bin 镜像（仅 ota 命令）")
    parser.add_argument("--name", help="要设置的蓝牙名称（仅 name 命令；省略则读取）")
    parser.add_argument("--confirm-ota", action="store_true", help="确认执行不可逆的设备 Flash 写入")
    parser.add_argument("command", choices=("scan", "info", "realtime", "params", "name", "faults", "ota-info", "ota"))
    args = parser.parse_args()
    if args.command == "scan":
        for address, name in await BleakTransport.scan():
            print(address, name)
        return
    transport = DemoTransport() if args.demo else BleakTransport()
    client = BmsClient(transport)
    await client.connect(args.address)
    try:
        if args.command == "info": print(await client.device_info())
        elif args.command == "realtime": print(await client.realtime())
        elif args.command == "params": print(*await client.all_parameters(), sep="\n")
        elif args.command == "name":
            if args.name is None:
                print(await client.ble_name())
            else:
                await client.set_ble_name(args.name)
                print("蓝牙名称已更新；实验室固件复位后恢复默认")
        elif args.command == "faults": print(await client.faults())
        elif args.command == "ota-info": print(await client.ota_info())
        else:
            if args.demo:
                parser.error("演示设备不支持实际 OTA")
            if args.image is None or not args.confirm_ota:
                parser.error("ota 必须同时提供 --image <镜像.bin> 和 --confirm-ota")
            def report(current: int, total: int) -> None:
                print(f"OTA: {current}/{total}")
            print(await client.ota_update(args.image, report))
    finally:
        await client.disconnect()


def main() -> None:
    asyncio.run(main_async())


if __name__ == "__main__":
    main()
