#!/usr/bin/env python3
"""BMS platform's reproducible local quality gates and firmware build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
import subprocess
import sys
from pathlib import Path
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = PROJECT_ROOT.parents[1]
BUILD_ROOT = PROJECT_ROOT / "build"
PC_CLIENT_ROOT = PROJECT_ROOT / "pc_client"
TC32_GCC = Path("C:/TelinkIoTStudio/opt/tc32/bin/tc32-elf-gcc.exe")
TC32_OBJCOPY = Path("C:/TelinkIoTStudio/opt/tc32/bin/tc32-elf-objcopy.exe")
TC32_SIZE = Path("C:/TelinkIoTStudio/opt/tc32/bin/tc32-elf-size.exe")
CPPCHECK = Path("C:/Program Files/cppcheck/cppcheck.exe")
FIRMWARE_STARTUP = SDK_ROOT / "boot" / "B85" / "cstartup_825x.S"
FIRMWARE_DIV_MOD = SDK_ROOT / "common" / "div_mod.S"
FIRMWARE_LINK_SCRIPT = SDK_ROOT / "project" / "tlsr_tc32" / "B85" / "boot.link"
FIRMWARE_CHECKER = SDK_ROOT / "script" / "tl_check_fw" / "tl_check_fw2.exe"

SOURCE_ROOTS = (PROJECT_ROOT / "board", PROJECT_ROOT / "core", PROJECT_ROOT / "afe", PROJECT_ROOT / "protocol")
EXPECTED_SOURCES = (
    Path("afe/afe_interface.c"),
    Path("afe/sh36735/sh36735_adapter.c"),
    Path("afe/sh36735/sh36735_driver.c"),
    Path("board/bms_product.c"),
    Path("core/bms_application.c"),
    Path("core/bms_balance.c"),
    Path("core/bms_config_store.c"),
    Path("core/bms_event.c"),
    Path("core/bms_heating.c"),
    Path("core/bms_parameters.c"),
    Path("core/bms_platform.c"),
    Path("core/bms_protection.c"),
    Path("core/bms_realtime.c"),
    Path("core/bms_soc.c"),
    Path("protocol/bmslink.c"),
)
FIRMWARE_OWNED_SOURCES = (
    Path("firmware/app.c"),
    Path("firmware/bms_firmware.c"),
    Path("firmware/bms_gatt.c"),
    Path("firmware/bms_lab_simulator.c"),
    Path("firmware/main.c"),
)
FIRMWARE_SDK_SOURCE_ROOTS = (
    SDK_ROOT / "common",
    SDK_ROOT / "drivers" / "B85",
    SDK_ROOT / "vendor" / "common",
    SDK_ROOT / "application" / "print",
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


def firmware_compiler_command(source: Path, output: Path,
                              defines: tuple[str, ...] = ()) -> list[str]:
    command = compiler_command(source, output)
    if not source.is_relative_to(PROJECT_ROOT):
        command.remove("-Werror")
    command.insert(command.index("-c"), "-Wno-unused-parameter")
    command.insert(command.index("-c"), "-Wno-ignored-qualifiers")
    command.insert(command.index("-c"), "-include")
    command.insert(command.index("-c"), str(PROJECT_ROOT / "firmware" / "app_config.h"))
    command.insert(command.index("-c"), "-I" + str(PROJECT_ROOT / "firmware"))
    for define in defines:
        command.insert(command.index("-include"), "-D" + define)
    return command


def firmware_sources() -> list[Path]:
    discovered = tuple(sorted(
        source.relative_to(PROJECT_ROOT)
        for source in (PROJECT_ROOT / "firmware").glob("*.c")
    ))
    if discovered != FIRMWARE_OWNED_SOURCES:
        expected = "\n  ".join(str(path) for path in FIRMWARE_OWNED_SOURCES)
        actual = "\n  ".join(str(path) for path in discovered)
        fail("固件源码清单发生变化；更新 tools/bms.py 后再构建。\n期望:\n  {}\n实际:\n  {}".format(expected, actual))
    sdk_sources = sorted(
        source
        for root in FIRMWARE_SDK_SOURCE_ROOTS
        for source in root.rglob("*.c")
    )
    if not sdk_sources:
        fail("未找到 SDK 固件运行时源码。")
    return [PROJECT_ROOT / source for source in discovered] + source_files() + sdk_sources


def firmware_object_path(source: Path, object_root: Path) -> Path:
    try:
        relative = source.relative_to(PROJECT_ROOT)
        return object_root / "project" / relative.with_suffix(".o")
    except ValueError:
        relative = source.relative_to(SDK_ROOT)
        return object_root / "sdk" / relative.with_suffix(".o")


def command_env(_: argparse.Namespace) -> None:
    require_file(TC32_GCC, "TC32 编译器")
    require_file(TC32_OBJCOPY, "TC32 Objcopy")
    require_file(TC32_SIZE, "TC32 Size")
    require_file(CPPCHECK, "Cppcheck")
    require_file(SDK_ROOT / "proj_lib" / "liblt_825x.a", "TLSR825x 协议栈库")
    require_file(SDK_ROOT / "proj_lib" / "liblt_general_stack.a", "通用协议栈库")
    require_file(FIRMWARE_STARTUP, "TLSR8251 启动文件")
    require_file(FIRMWARE_DIV_MOD, "TLSR825x 除法与 CRC 运行时")
    require_file(FIRMWARE_LINK_SCRIPT, "TLSR825x 链接脚本")
    require_file(FIRMWARE_CHECKER, "Telink 固件检查器")
    run([str(TC32_GCC), "--version"])
    run([str(CPPCHECK), "--version"])
    print("SDK 根目录: {}".format(SDK_ROOT))
    print("完整固件阶段固定选用 MCU_STARTUP_8251。")


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


def build_firmware(output_name: str, profile: str, defines: tuple[str, ...]) -> None:
    require_file(TC32_GCC, "TC32 编译器")
    require_file(TC32_OBJCOPY, "TC32 Objcopy")
    require_file(TC32_SIZE, "TC32 Size")
    require_file(FIRMWARE_STARTUP, "TLSR8251 启动文件")
    require_file(FIRMWARE_DIV_MOD, "TLSR825x 除法与 CRC 运行时")
    require_file(FIRMWARE_LINK_SCRIPT, "TLSR825x 链接脚本")
    require_file(SDK_ROOT / "proj_lib" / "liblt_825x.a", "TLSR825x 协议栈库")

    output_root = BUILD_ROOT / output_name
    object_root = output_root / "obj"
    object_root.mkdir(parents=True, exist_ok=True)
    objects: list[Path] = []
    for source in firmware_sources():
        output = firmware_object_path(source, object_root)
        output.parent.mkdir(parents=True, exist_ok=True)
        run(firmware_compiler_command(source, output, defines))
        objects.append(output)

    startup_object = object_root / "startup" / "cstartup_825x.o"
    startup_object.parent.mkdir(parents=True, exist_ok=True)
    run([
        str(TC32_GCC),
        "-DMCU_STARTUP_8251=1",
        "-c", str(FIRMWARE_STARTUP),
        "-o", str(startup_object),
    ])
    objects.insert(0, startup_object)

    div_mod_object = object_root / "startup" / "div_mod.o"
    run([
        str(TC32_GCC),
        "-DMCU_STARTUP_8251=1",
        "-c", str(FIRMWARE_DIV_MOD),
        "-o", str(div_mod_object),
    ])
    objects.insert(1, div_mod_object)

    elf_path = output_root / "telink_bms.elf"
    run([
        str(TC32_GCC),
        "-nostdlib",
        "-Wl,--gc-sections",
        "-Wl,-T," + str(FIRMWARE_LINK_SCRIPT),
        "-o", str(elf_path),
    ] + [str(path) for path in objects] + [
        "-L" + str(SDK_ROOT / "proj_lib"),
        "-llt_825x",
    ])
    binary_path = output_root / "telink_bms.bin"
    run([str(TC32_OBJCOPY), "-O", "binary", str(elf_path), str(binary_path)])
    run([str(TC32_SIZE), str(elf_path)])
    require_file(FIRMWARE_CHECKER, "Telink 固件检查器")
    run([str(FIRMWARE_CHECKER), str(binary_path)])

    manifest = {
        "target": "tlsr8251-bms-firmware",
        "profile": profile,
        "defines": list(defines),
        "startup_define": "MCU_STARTUP_8251=1",
        "startup": str(FIRMWARE_STARTUP.relative_to(SDK_ROOT)),
        "runtime_assembly": str(FIRMWARE_DIV_MOD.relative_to(SDK_ROOT)),
        "link_script": str(FIRMWARE_LINK_SCRIPT.relative_to(SDK_ROOT)),
        "library": "proj_lib/liblt_825x.a",
        "sources": [
            {
                "path": str(source.relative_to(PROJECT_ROOT)) if source.is_relative_to(PROJECT_ROOT)
                else "sdk/" + str(source.relative_to(SDK_ROOT)),
                "sha256": sha256(source),
            }
            for source in firmware_sources()
        ],
    }
    manifest_path = output_root / "build_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("已生成可检查的 TLSR8251 {}固件: {}".format(profile, binary_path))


def command_build_firmware(_: argparse.Namespace) -> None:
    build_firmware("firmware", "production", ())


def command_build_lab_firmware(_: argparse.Namespace) -> None:
    build_firmware("lab_firmware", "afe-simulator", ("BMS_LAB_SIMULATOR_ENABLE=1",))


def command_build_lab_ota_firmware(_: argparse.Namespace) -> None:
    build_firmware("lab_ota_firmware", "afe-simulator-ota", (
        "BMS_LAB_SIMULATOR_ENABLE=1", "BMS_LAB_OTA_ENABLE=1"))


def command_build_lab_ota_proof_firmware(_: argparse.Namespace) -> None:
    build_firmware("lab_ota_proof_firmware", "afe-simulator-ota-proof", (
        "BMS_LAB_SIMULATOR_ENABLE=1", "BMS_LAB_OTA_ENABLE=1",
        "BMS_FIRMWARE_VERSION_PATCH=1"))


def command_build_lab_ota_control_firmware(_: argparse.Namespace) -> None:
    build_firmware("lab_ota_control_firmware", "afe-simulator-ota-control", (
        "BMS_LAB_SIMULATOR_ENABLE=1", "BMS_LAB_OTA_ENABLE=1",
        "BMS_FIRMWARE_VERSION_PATCH=2"))


def command_build_pc_exe(_: argparse.Namespace) -> None:
    entry_point = PC_CLIENT_ROOT / "run_gui.py"
    require_file(entry_point, "PC 上位机图形入口")
    output_root = BUILD_ROOT / "pc_client"
    output_root.mkdir(parents=True, exist_ok=True)
    temporary_base = Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir()))
    temporary_base = temporary_base / "CodexTemp" / "bms_platform" / "pyinstaller"
    temporary_base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="build-pc-exe-", dir=temporary_base) as temporary_directory:
        temporary_root = Path(temporary_directory)
        run([
            sys.executable,
            "-m",
            "PyInstaller",
            "--noconfirm",
            "--clean",
            "--windowed",
            "--onefile",
            "--name",
            "TelinkBMS",
            "--paths",
            str(PC_CLIENT_ROOT),
            "--collect-all",
            "bleak",
            "--collect-all",
            "winrt",
            "--distpath",
            str(output_root),
            "--workpath",
            str(temporary_root / "work"),
            "--specpath",
            str(temporary_root / "spec"),
            str(entry_point),
        ])
    executable_path = output_root / "TelinkBMS.exe"
    require_file(executable_path, "PC 上位机 EXE")
    manifest_path = output_root / "build_manifest.json"
    manifest_path.write_text(json.dumps({
        "target": "telink-bms-pc-client",
        "entry_point": "pc_client/run_gui.py",
        "output": executable_path.name,
        "python": sys.version,
        "sha256": sha256(executable_path),
        "packaging": "PyInstaller onefile windowed; Bleak and WinRT collected",
    }, indent=2) + "\n", encoding="utf-8")
    print("已生成可双击运行的 Windows 上位机: {}".format(executable_path))


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
    pc_client_root = PROJECT_ROOT / "pc_client"
    pc_command = [sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py"]
    print("+ " + " ".join(pc_command))
    result = subprocess.run(pc_command, cwd=str(pc_client_root), env=environment, check=False)
    if result.returncode != 0:
        fail("PC 客户端测试失败，退出码为 {}".format(result.returncode))
    print("协议测试通过。")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Telink BMS 平台质量门禁")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("env", help="验证固定工具链与 SDK 依赖").set_defaults(handler=command_env)
    subparsers.add_parser("build-core", help="仅编译 BMS 自有 C 源码").set_defaults(handler=command_build_core)
    subparsers.add_parser("build-firmware", help="构建并检查 TLSR8251 BLE 固件").set_defaults(handler=command_build_firmware)
    subparsers.add_parser("build-lab-firmware", help="构建不访问 AFE/GPIO 的 BLE 通信模拟固件").set_defaults(handler=command_build_lab_firmware)
    subparsers.add_parser("build-lab-ota-firmware", help="构建官方开发板专用的模拟数据 + OTA 实验固件").set_defaults(handler=command_build_lab_ota_firmware)
    subparsers.add_parser("build-lab-ota-proof-firmware", help="构建版本 0.2.1 的 OTA 启动验证镜像").set_defaults(handler=command_build_lab_ota_proof_firmware)
    subparsers.add_parser("build-lab-ota-control-firmware", help="构建版本 0.2.2 的自动刷新和蓝牙名实验镜像").set_defaults(handler=command_build_lab_ota_control_firmware)
    subparsers.add_parser("build-pc-exe", help="生成可双击运行的 Windows 上位机 EXE").set_defaults(handler=command_build_pc_exe)
    subparsers.add_parser("static", help="对 BMS 自有源码运行 Cppcheck").set_defaults(handler=command_static)
    subparsers.add_parser("test", help="运行不依赖硬件的协议测试").set_defaults(handler=command_test)
    return parser


def main(argv: Iterable[str]) -> None:
    arguments = create_parser().parse_args(list(argv))
    arguments.handler(arguments)


if __name__ == "__main__":
    main(sys.argv[1:])
