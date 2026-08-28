#!/usr/bin/env python3
"""HS-D011 / TLSR8251 command-line build entry point.

The build keeps the repository's Telink 825x_ble_sample C ABI/link contract,
but uses the startup SRAM profile for the actual TLSR8251 device. It does not
depend on opening Telink IDE or on IDE-generated makefiles.

Typical use:
    python bms_tools/bms.py env
    python bms_tools/bms.py sources --check
    python bms_tools/bms.py rebuild --jobs 4
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
SDK_DIR = REPO_ROOT / "tc_ble_single_sdk"
PROJ_DIR = SDK_DIR / "project" / "tlsr_tc32" / "B85"
LINKER_FILE = PROJ_DIR / "boot.link"
PROJ_LIB_DIR = SDK_DIR / "proj_lib"
BUILD_DIR = PROJ_DIR / "825x_ble_sample_cli"
OBJ_DIR = BUILD_DIR / "obj"
GEN_DIR = BUILD_DIR / "gen"
ELF = BUILD_DIR / "825x_ble_sample.elf"
RAW_BIN = BUILD_DIR / "825x_ble_sample.raw.bin"
BIN = BUILD_DIR / "825x_ble_sample.bin"
LST = GEN_DIR / "825x_ble_sample.lst"
MAP = GEN_DIR / "825x_ble_sample.map"
MANIFEST = BUILD_DIR / "fw_manifest.json"
SOURCE_ORDER_FILE = HERE / "source_order.txt"
BUILD_MK = HERE / "build.mk"
STARTUP_FILE = SDK_DIR / "boot" / "B85" / "cstartup_825x.S"

TL_CHECK_DIR = SDK_DIR / "script" / "tl_check_fw"
TL_CHECK_WIN = TL_CHECK_DIR / "tl_check_fw2.exe"
TL_CHECK_LINUX = TL_CHECK_DIR / "check_fw"

VENDOR_LIBS = (
    PROJ_LIB_DIR / "liblt_825x.a",
    PROJ_LIB_DIR / "liblt_general_stack.a",
)

SOURCE_GROUPS = (
    (Path("vendor/common"), False),
    (Path("vendor/ble_sample"), True),
    (Path("drivers/B85"), False),
    (Path("drivers/B85/flash"), False),
    (Path("drivers/B85/driver_ext"), False),
    (Path("common"), False),
    (Path("boot/B85"), False),
    (Path("application/usbstd"), False),
    (Path("application/print"), False),
    (Path("application/keyboard"), False),
    (Path("application/audio"), False),
    (Path("application/app"), False),
)

DEFAULT_TC32_DIR = Path(r"C:\TelinkIoTStudio\opt\tc32\bin")
DEFAULT_MAKE = Path(r"C:\qp\qtools\bin\make.exe")

TARGET_MCU = "TLSR8251F512"
STARTUP_PROFILE = "MCU_STARTUP_8251"
SRAM_BASE = 0x840000
SRAM_BYTES = 0x8000
SRAM_END = SRAM_BASE + SRAM_BYTES
MAIN_STACK_GUARD_BYTES = 600


def info(msg: str) -> None:
    print(f"[bms] {msg}")


def die(msg: str, code: int = 2) -> None:
    print(f"[bms] ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def rel(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def command_version(command: list[str]) -> str:
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        lines = (result.stdout or "").strip().splitlines()
        return lines[0] if lines else f"exit={result.returncode}"
    except OSError as exc:
        return f"unavailable: {exc}"


def toolchain_env() -> dict[str, str]:
    env = dict(os.environ)
    key = next((k for k in env if k.upper() == "PATH"), "PATH")
    current = env.get(key, "")
    requested = os.environ.get("TC32_TOOLCHAIN_BIN", "").strip()

    prepend = None
    if requested:
        p = Path(requested).expanduser()
        if not p.exists():
            die(f"TC32_TOOLCHAIN_BIN does not exist: {p}")
        prepend = p
    elif shutil.which("tc32-elf-gcc", path=current) is None and DEFAULT_TC32_DIR.exists():
        prepend = DEFAULT_TC32_DIR

    for k in [k for k in env if k.upper() == "PATH"]:
        env.pop(k, None)
    env["PATH"] = (str(prepend) + os.pathsep + current) if prepend else current
    return env


def find_tool(name: str, env: dict[str, str] | None = None) -> str:
    env = env or toolchain_env()
    found = shutil.which(name, path=env.get("PATH", ""))
    if found:
        return found
    suffix = ".exe" if os.name == "nt" else ""
    candidate = DEFAULT_TC32_DIR / f"{name}{suffix}"
    if candidate.exists():
        return str(candidate)
    die(f"missing tool: {name}; set TC32_TOOLCHAIN_BIN or install Telink TC32 GCC")
    return ""


def find_make() -> str:
    configured = os.environ.get("MAKE", "").strip()
    if configured:
        return configured
    found = shutil.which("make") or shutil.which("gmake")
    if found:
        return found
    if DEFAULT_MAKE.exists():
        return str(DEFAULT_MAKE)
    die("GNU Make not found. Install make or set MAKE; qtools fallback is also missing.")
    return ""


def validate_target_contract() -> None:
    if not BUILD_MK.exists():
        die(f"build driver missing: {BUILD_MK}")
    if not STARTUP_FILE.exists():
        die(f"startup source missing: {STARTUP_FILE}")

    build_text = BUILD_MK.read_text(encoding="utf-8", errors="replace")
    startup_text = STARTUP_FILE.read_text(encoding="utf-8", errors="replace")

    if "AFLAGS_BASE := -DMCU_STARTUP_8251" not in build_text:
        die("build.mk is not selecting MCU_STARTUP_8251")
    if "AFLAGS_BASE := -DMCU_STARTUP_8258" in build_text:
        die("build.mk still contains an active MCU_STARTUP_8258 startup selection")

    required_startup_fragments = (
        "#elif (MCU_STARTUP_8251)",
        "SRAM_BASE_ADDR + SRAM_32K",
        ".word\t(SRAM_SIZE)",
    )
    missing = [item for item in required_startup_fragments if item not in startup_text]
    if missing:
        die("unexpected cstartup_825x.S layout; missing: " + ", ".join(missing))


def discover_sources() -> list[str]:
    result: list[str] = []
    for group, recursive in SOURCE_GROUPS:
        root = SDK_DIR / group
        if not root.exists():
            die(f"managed source group is missing: {root}")
        candidates = list(root.rglob("*") if recursive else root.glob("*"))
        files = [p for p in candidates if p.is_file() and p.suffix in (".c", ".S")]
        files.sort(key=lambda p: p.relative_to(SDK_DIR).as_posix().casefold())
        result.extend(p.relative_to(SDK_DIR).as_posix() for p in files)
    return result


def read_source_order() -> list[str]:
    if not SOURCE_ORDER_FILE.exists():
        die(f"source order file missing: {SOURCE_ORDER_FILE}")
    order = []
    for raw in SOURCE_ORDER_FILE.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            order.append(line)
    return order


def validate_source_order(verbose: bool = True) -> list[str]:
    order = read_source_order()
    discovered = discover_sources()

    if len(order) != len(set(order)):
        die("source_order.txt contains duplicate entries")

    missing_files = [p for p in order if not (SDK_DIR / p).is_file()]
    if missing_files:
        die("locked sources no longer exist:\n  " + "\n  ".join(missing_files))

    unlisted = [p for p in discovered if p not in order]
    stale = [p for p in order if p not in discovered]
    if unlisted or stale:
        lines = ["source/link order is out of date"]
        if unlisted:
            lines.append("unlisted new sources:\n  " + "\n  ".join(unlisted))
        if stale:
            lines.append("stale locked sources:\n  " + "\n  ".join(stale))
        lines.append("review then run: python bms_tools/bms.py sources --update")
        die("\n".join(lines))

    if verbose:
        digest = hashlib.sha256(("\n".join(order) + "\n").encode()).hexdigest()
        info(f"source order OK: {len(order)} objects, sha256={digest}")
    return order


def write_source_order() -> None:
    sources = discover_sources()
    header = (
        "# HS-D011 TLSR8251 authoritative source/link order.\n"
        "# Keep this file under review: object order affects the final Telink image.\n"
        "# Update explicitly with: python bms_tools/bms.py sources --update\n"
    )
    SOURCE_ORDER_FILE.write_text(header + "\n".join(sources) + "\n", encoding="utf-8")
    info(f"updated {SOURCE_ORDER_FILE}; review the Git diff before committing")


def generate_sources_mk(order: list[str]) -> None:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    GEN_DIR.mkdir(parents=True, exist_ok=True)

    obj_exprs = []
    rules = []
    for source in order:
        src = Path(source)
        obj_rel = src.with_suffix(".o")
        (OBJ_DIR / obj_rel.parent).mkdir(parents=True, exist_ok=True)

        obj = f"$(OBJ_DIR)/{obj_rel.as_posix()}"
        src_expr = f"$(SDK_DIR)/{src.as_posix()}"
        obj_exprs.append(obj)
        rules.append("")
        rules.append(f"{obj}: {src_expr}")
        if src.suffix == ".S":
            rules.append(f"\t@echo Assembling: {source}")
            rules.append('\t$(CC) $(AFLAGS) -c -o"$@" "$<"')
        else:
            rules.append(f"\t@echo Building: {source}")
            rules.append('\t$(CC) $(CFLAGS) -c -o"$@" "$<"')

    lines = ["# generated by bms_tools/bms.py; do not edit", "OBJS := \\"]
    for i, obj in enumerate(obj_exprs):
        end = " \\" if i != len(obj_exprs) - 1 else ""
        lines.append(f"\t{obj}{end}")
    lines.extend(rules)
    (BUILD_DIR / "sources.mk").write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_make(jobs: int) -> None:
    validate_target_contract()
    order = validate_source_order()
    generate_sources_mk(order)
    env = toolchain_env()

    # Resolve these up front so failure is immediate and diagnostic.
    for tool in ("tc32-elf-gcc", "tc32-elf-ld", "tc32-elf-objcopy", "tc32-elf-objdump", "tc32-elf-size"):
        find_tool(tool, env)

    make = find_make()
    cmd = [
        make,
        "-f",
        "bms_tools/build.mk",
        "-j",
        str(max(1, jobs)),
        "REPO_ROOT=.",
        "SDK_DIR=tc_ble_single_sdk",
        "BUILD_DIR=tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli",
        "all",
    ]
    info(f"starting TC32 build without Telink IDE ({TARGET_MCU}, {STARTUP_PROFILE})")
    result = subprocess.run(cmd, cwd=REPO_ROOT, env=env, check=False)
    if result.returncode != 0:
        die(f"build failed, make exit code={result.returncode}", result.returncode)


def firmware_checker() -> Path:
    if os.name == "nt":
        return TL_CHECK_WIN
    return TL_CHECK_LINUX


def finalize_firmware() -> None:
    if not RAW_BIN.exists():
        die(f"raw BIN missing after build: {RAW_BIN}")
    checker = firmware_checker()
    if not checker.exists():
        die(f"Telink firmware checker missing: {checker}")

    BIN.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(RAW_BIN, BIN)
    if os.name != "nt":
        try:
            checker.chmod(checker.stat().st_mode | 0o111)
        except OSError:
            pass

    info(f"running official Telink firmware post-check: {checker.name}")
    result = subprocess.run([str(checker.resolve()), str(BIN.resolve())], cwd=checker.parent, check=False)
    if result.returncode != 0:
        die(f"Telink firmware checker failed, exit code={result.returncode}", result.returncode)


def git_head() -> str | None:
    try:
        r = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, check=False,
        )
        return r.stdout.strip() if r.returncode == 0 else None
    except OSError:
        return None


def map_symbol_value(text: str, symbol: str) -> int | None:
    patterns = (
        rf"^\s*(0x[0-9a-fA-F]+)\s+{re.escape(symbol)}\b",
        rf"\b{re.escape(symbol)}\b\s*=\s*(0x[0-9a-fA-F]+)",
        rf"\b{re.escape(symbol)}\b.*?(0x[0-9a-fA-F]+)",
    )
    for pattern in patterns:
        match = re.search(pattern, text, re.M)
        if match:
            return int(match.group(1), 16)
    return None


def validate_map_sram() -> None:
    if not MAP.exists():
        die(f"MAP missing after link: {MAP}")
    text = MAP.read_text(encoding="utf-8", errors="replace")
    ram_end = map_symbol_value(text, "_ram_use_end_")
    linked_sram_end = map_symbol_value(text, "__SRAM_SIZE")

    if linked_sram_end is not None and linked_sram_end != SRAM_END:
        die(
            f"linked __SRAM_SIZE=0x{linked_sram_end:06X}, expected TLSR8251 end 0x{SRAM_END:06X}"
        )

    if ram_end is not None:
        limit = SRAM_END - MAIN_STACK_GUARD_BYTES
        if ram_end >= limit:
            die(
                f"RAM overflow/stack margin violation: _ram_use_end_=0x{ram_end:06X}, "
                f"limit=0x{limit:06X}"
            )
        info(
            f"TLSR8251 SRAM check: ram_use_end=0x{ram_end:06X}, "
            f"stack/headroom={SRAM_END - ram_end} bytes"
        )
    else:
        info("TLSR8251 SRAM check: linker ASSERT active; MAP symbol parser did not locate _ram_use_end_")


def write_manifest() -> None:
    if not BIN.exists() or not ELF.exists():
        die("build artifacts missing; run build/rebuild first")
    order = validate_source_order(verbose=False)
    inputs = [BUILD_MK, SOURCE_ORDER_FILE, LINKER_FILE, STARTUP_FILE, *VENDOR_LIBS]
    manifest = {
        "format": 2,
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "git_head": git_head(),
        "target": f"{TARGET_MCU} / B85 / 825x_ble_sample",
        "startup_profile": STARTUP_PROFILE,
        "sram": {
            "base": f"0x{SRAM_BASE:06X}",
            "size_bytes": SRAM_BYTES,
            "end_exclusive": f"0x{SRAM_END:06X}",
            "linker_stack_guard_bytes": MAIN_STACK_GUARD_BYTES,
        },
        "source_count": len(order),
        "source_order_sha256": hashlib.sha256(("\n".join(order) + "\n").encode()).hexdigest(),
        "artifacts": {
            "elf": {"path": rel(ELF), "size": ELF.stat().st_size, "sha256": sha256(ELF)},
            "bin": {"path": rel(BIN), "size": BIN.stat().st_size, "sha256": sha256(BIN)},
            "raw_bin": {"path": rel(RAW_BIN), "size": RAW_BIN.stat().st_size, "sha256": sha256(RAW_BIN)},
        },
        "build_inputs": {
            rel(p): sha256(p) for p in inputs if p.exists()
        },
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    info(f"manifest: {MANIFEST}")


def verify_manifest() -> None:
    if not MANIFEST.exists():
        die("manifest missing; run build/rebuild or manifest first")
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    failures = []
    for name, item in data.get("artifacts", {}).items():
        p = REPO_ROOT / item["path"]
        if not p.exists():
            failures.append(f"{name}: missing {p}")
            continue
        actual = sha256(p)
        if actual != item["sha256"]:
            failures.append(f"{name}: sha256 mismatch")
    for path_text, expected in data.get("build_inputs", {}).items():
        p = REPO_ROOT / path_text
        if not p.exists() or sha256(p) != expected:
            failures.append(f"build input changed: {path_text}")
    if failures:
        die("manifest verification failed:\n  " + "\n  ".join(failures))
    info("manifest verification PASS")


def cmd_env(_: argparse.Namespace) -> int:
    env = toolchain_env()
    validate_target_contract()
    print(f"repo              : {REPO_ROOT}")
    print(f"sdk               : {SDK_DIR} (exists={SDK_DIR.exists()})")
    print(f"linker            : {LINKER_FILE} (exists={LINKER_FILE.exists()})")
    print(f"startup           : {STARTUP_FILE} (exists={STARTUP_FILE.exists()})")
    print(f"build output      : {BUILD_DIR}")
    for lib in VENDOR_LIBS:
        print(f"vendor library    : {lib} (exists={lib.exists()})")
    print(f"fw checker win    : {TL_CHECK_WIN} (exists={TL_CHECK_WIN.exists()})")
    print(f"fw checker linux  : {TL_CHECK_LINUX} (exists={TL_CHECK_LINUX.exists()})")
    print(f"python            : {sys.version.split()[0]} ({sys.executable})")
    print(f"make              : {find_make()}")
    gcc = find_tool("tc32-elf-gcc", env)
    print(f"tc32-elf-gcc      : {gcc}")
    print(f"compiler version  : {command_version([gcc, '--version'])}")
    validate_source_order()
    print(f"target MCU        : {TARGET_MCU}")
    print(f"startup profile   : {STARTUP_PROFILE}")
    print(f"SRAM              : 0x{SRAM_BASE:06X}..0x{SRAM_END - 1:06X} ({SRAM_BYTES // 1024} KiB)")
    print(f"main SP after reset: 0x{SRAM_END:06X}")
    print(f"linker RAM guard  : _ram_use_end_ < 0x{SRAM_END - MAIN_STACK_GUARD_BYTES:06X}")
    return 0


def cmd_sources(args: argparse.Namespace) -> int:
    if args.source_action == "update":
        write_source_order()
    else:
        validate_source_order()
    return 0


def do_build(jobs: int, clean: bool) -> None:
    if clean and BUILD_DIR.exists():
        info(f"removing previous CLI output: {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)
    run_make(jobs)
    validate_map_sram()
    finalize_firmware()
    write_manifest()
    info(f"ELF : {ELF}")
    info(f"BIN : {BIN}")
    info(f"MAP : {MAP}")
    info(f"LST : {LST}")


def cmd_build(args: argparse.Namespace) -> int:
    do_build(args.jobs, clean=False)
    return 0


def cmd_rebuild(args: argparse.Namespace) -> int:
    do_build(args.jobs, clean=True)
    return 0


def cmd_check_fw(_: argparse.Namespace) -> int:
    if not RAW_BIN.exists():
        die("raw BIN missing; run build/rebuild first")
    validate_map_sram()
    finalize_firmware()
    write_manifest()
    info("official firmware post-check PASS")
    return 0


def cmd_size(_: argparse.Namespace) -> int:
    if not ELF.exists():
        die("ELF missing; run build/rebuild first")
    env = toolchain_env()
    tool = find_tool("tc32-elf-size", env)
    return subprocess.run([tool, "-t", str(ELF)], env=env, check=False).returncode


def cmd_map(_: argparse.Namespace) -> int:
    if not MAP.exists():
        die("MAP missing; run build/rebuild first")
    text = MAP.read_text(encoding="utf-8", errors="replace")
    print(f"MAP: {MAP} ({MAP.stat().st_size} bytes)")
    for symbol in ("_bin_size_", "_code_size_", "_ram_use_end_", "_start_bss_", "_end_bss_", "__SRAM_SIZE"):
        value = map_symbol_value(text, symbol)
        if value is not None:
            print(f"{symbol:<16} 0x{value:06X}")
    ram_end = map_symbol_value(text, "_ram_use_end_")
    if ram_end is not None:
        print(f"TLSR8251 SRAM end  0x{SRAM_END:06X}")
        print(f"RAM headroom       {SRAM_END - ram_end} bytes (includes main-stack region)")
        print(f"linker guard       {MAIN_STACK_GUARD_BYTES} bytes minimum")
    validate_map_sram()
    return 0


def cmd_manifest(_: argparse.Namespace) -> int:
    write_manifest()
    return 0


def cmd_verify(_: argparse.Namespace) -> int:
    verify_manifest()
    return 0


def cmd_ci(args: argparse.Namespace) -> int:
    do_build(args.jobs, clean=True)
    verify_manifest()
    return 0


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="HS-D011 TLSR8251 no-IDE build tooling")
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("env", help="check local compiler/build environment")
    s.set_defaults(func=cmd_env)

    s = sub.add_parser("sources", help="check/update the locked source/link order")
    s.add_argument("--check", dest="source_action", action="store_const", const="check", default="check")
    s.add_argument("--update", dest="source_action", action="store_const", const="update")
    s.set_defaults(func=cmd_sources)

    for name, func, help_text in (
        ("build", cmd_build, "incremental command-line build"),
        ("rebuild", cmd_rebuild, "clean command-line rebuild"),
        ("ci", cmd_ci, "clean build plus manifest verification"),
    ):
        s = sub.add_parser(name, help=help_text)
        s.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
        s.set_defaults(func=func)

    s = sub.add_parser("check-fw", help="regenerate canonical BIN and run Telink checker")
    s.set_defaults(func=cmd_check_fw)
    s = sub.add_parser("size", help="print TC32 ELF size")
    s.set_defaults(func=cmd_size)
    s = sub.add_parser("map", help="show key MAP symbols and TLSR8251 SRAM margin")
    s.set_defaults(func=cmd_map)
    s = sub.add_parser("manifest", help="write build integrity manifest")
    s.set_defaults(func=cmd_manifest)
    s = sub.add_parser("verify", help="verify artifacts/build inputs against manifest")
    s.set_defaults(func=cmd_verify)
    return p


def main() -> int:
    args = parser().parse_args()
    return int(args.func(args) or 0)


if __name__ == "__main__":
    raise SystemExit(main())
