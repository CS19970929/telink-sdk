"""Headless PC-client entry point, useful for BLE lab and automation use."""

from __future__ import annotations

import argparse
import asyncio

from .client import BmsClient
from .transport import BleakTransport, DemoTransport


async def main_async() -> None:
    parser = argparse.ArgumentParser(description="Telink BMSLink PC 客户端")
    parser.add_argument("--demo", action="store_true", help="使用离线演示设备")
    parser.add_argument("--address", help="目标 BLE 地址")
    parser.add_argument("command", choices=("scan", "info", "realtime", "params", "faults", "ota-info"))
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
        elif args.command == "faults": print(await client.faults())
        else: print(await client.ota_info())
    finally:
        await client.disconnect()


def main() -> None:
    asyncio.run(main_async())


if __name__ == "__main__":
    main()
