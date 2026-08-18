#!/usr/bin/env python3
"""BMS platform's reproducible local quality gates.

The platform deliberately builds only its owned sources at this stage.  BLE
startup/linker integration is a later milestone after the board pinout is
available and the TLSR8251 startup selection can be verified on hardware.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = PROJECT_ROOT.parents[1]
BUILD_ROOT = PROJECT_ROOT / "build"
TC32_GCC = Path("C:/TelinkIoTStudio/opt/tc32/bin/tc32-elf-gcc.exe")
CPPCHECK = Path("C:/Program Files/cppcheck/cppcheck.exe")

SOURCE_ROOTS = (PROJECT_ROOT / "board", PROJECT_ROOT / "core", PROJECT_ROOT / "afe", PROJECT_ROOT / "protocol")
EXPECTED_SOURCES = (
    Path("afe/afe_interface.c"),
    Path("afe/sh36735/sh36735_adapter.c"),
    Path("afe/sh36735/sh36735_driver.c"),
    Path("board/bms_product.c"),
    Path("core/bms_platform.c"),
    Path("core/bms_realtime.c"),
    Path("protocol/bmslink.c"),
)


def fail(message: str) -> None:
    print("错误: " + message, file=sys.stderr)
    raise SystemExit(2)


def run(command: list[str], cwd: Path = PROJECT_ROOT) -> None:
    print("+ " + " ".join(command))
    result = subprocess.run(command, cwd=str(cwd), check=False)
    if result.returncode != 0:
        fail("命令失败，退出码为 {}".format(result.returncode))


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        fail("未找到{}: {}".format(label, path))


def source_files() -> list[Path]:
    discovered = sorted(
        source.relative_to(PROJECT_ROOT)
        for root in SOURCE_ROOTS
        for source in root.rglob("*.c")
    )
    if tuple(discovered) != EXPECTED_SOURCES:
        expected = "\n  ".join(str(path) for path in EXPECTED_SOURCES)
        actual = "\n  ".join(str(path) for path in discovered)
        fail("源码清单发生变化；更新 tools/bms.py 后再构建。\n期望:\n  {}\n实际:\n  {}".format(expected, actual))
    return [PROJECT_ROOT / source for source in discovered]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as input_file:
        for block in iter(lambda: input_file.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def compiler_command(source: Path, output: Path) -> list[str]:
    return [
        str(TC32_GCC),
        "-std=gnu99",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-ffunction-sections",
        "-fdata-sections",
        "-fpack-struct",
        "-fshort-enums",
        "-finline-small-functions",
        "-fshort-wchar",
        "-fms-extensions",
        "-DCHIP_TYPE=CHIP_TYPE_825x",
        "-D__PROJECT_8251_BMS_PLATFORM__=1",
        "-I" + str(PROJECT_ROOT / "include"),
        "-I" + str(SDK_ROOT),
        "-I" + str(SDK_ROOT / "vendor" / "common"),
        "-I" + str(SDK_ROOT / "common"),
        "-I" + str(SDK_ROOT / "drivers" / "B85"),
        "-c",
        str(source),
        "-o",
        str(output),
    ]


def command_env(_: argparse.Namespace) -> None:
    require_file(TC32_GCC, "TC32 编译器")
    require_file(CPPCHECK, "Cppcheck")
    require_file(SDK_ROOT / "proj_lib" / "liblt_825x.a", "TLSR825x 协议栈库")
    require_file(SDK_ROOT / "proj_lib" / "liblt_general_stack.a", "通用协议栈库")
    run([str(TC32_GCC), "--version"])
    run([str(CPPCHECK), "--version"])
    print("SDK 根目录: {}".format(SDK_ROOT))
    print("完整固件阶段必须选用 MCU_STARTUP_8251；当前 build-core 不产生可烧录镜像。")


def command_build_core(_: argparse.Namespace) -> None:
    require_file(TC32_GCC, "TC32 编译器")
    sources = source_files()
    object_root = BUILD_ROOT / "tc32" / "obj"
    object_root.mkdir(parents=True, exist_ok=True)

    for source in sources:
        relative = source.relative_to(PROJECT_ROOT).with_suffix(".o")
        output = object_root / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        run(compiler_command(source, output))

    manifest = {
        "target": "tlsr8251-core-gate",
        "compiler": str(TC32_GCC),
        "sources": [
            {"path": str(source.relative_to(PROJECT_ROOT)), "sha256": sha256(source)}
            for source in sources
        ],
        "note": "Only project-owned objects; no BLE startup, linker or firmware image.",
    }
    manifest_path = BUILD_ROOT / "tc32" / "build_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("已生成 {} 个 TC32 目标文件。".format(len(sources)))
    print("清单: {}".format(manifest_path))


def command_static(_: argparse.Namespace) -> None:
    require_file(CPPCHECK, "Cppcheck")
    sources = source_files()
    result_dir = BUILD_ROOT / "static"
    result_dir.mkdir(parents=True, exist_ok=True)
    result_path = result_dir / "cppcheck.xml"
    command = [
        str(CPPCHECK),
        "--enable=warning,style,performance,portability",
        "--std=c99",
        "--language=c",
        "--inline-suppr",
        "--quiet",
        "--xml",
        "--xml-version=2",
        "--error-exitcode=1",
        "--suppress=missingIncludeSystem",
        "-I" + str(PROJECT_ROOT / "include"),
    ] + [str(source) for source in sources]
    print("+ " + " ".join(command))
    with result_path.open("w", encoding="utf-8") as output_file:
        result = subprocess.run(command, cwd=str(PROJECT_ROOT), stdout=subprocess.DEVNULL,
                                stderr=output_file, check=False)
    if result.returncode != 0:
        fail("Cppcheck 发现问题，报告: {}".format(result_path))
    print("Cppcheck 通过，报告: {}".format(result_path))


def command_test(_: argparse.Namespace) -> None:
    environment = dict(__import__("os").environ)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    command = [sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py"]
    print("+ " + " ".join(command))
    result = subprocess.run(command, cwd=str(PROJECT_ROOT), env=environment, check=False)
    if result.returncode != 0:
        fail("协议测试失败，退出码为 {}".format(result.returncode))
    print("协议测试通过。")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Telink BMS 平台质量门禁")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("env", help="验证固定工具链与 SDK 依赖").set_defaults(handler=command_env)
    subparsers.add_parser("build-core", help="仅编译 BMS 自有 C 源码").set_defaults(handler=command_build_core)
    subparsers.add_parser("static", help="对 BMS 自有源码运行 Cppcheck").set_defaults(handler=command_static)
    subparsers.add_parser("test", help="运行不依赖硬件的协议测试").set_defaults(handler=command_test)
    return parser


def main(argv: Iterable[str]) -> None:
    arguments = create_parser().parse_args(list(argv))
    arguments.handler(arguments)


if __name__ == "__main__":
    main(sys.argv[1:])
