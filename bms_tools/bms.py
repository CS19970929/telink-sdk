#!/usr/bin/env python3
"""TLSR8251 multi-board/multi-AFE command-line build entry point.

The build keeps the Telink 825x_ble_sample C ABI/link contract while selecting
both the physical BMS board profile and the AFE backend at compile time. It does
not depend on opening Telink IDE or on IDE-generated makefiles.

Typical use:
    python bms_tools/bms.py profiles
    python bms_tools/bms.py env --board legacy-309 --afe sh367309
    python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
    python bms_tools/bms.py verify
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
BUILD_ROOT = PROJ_DIR / "825x_ble_sample_cli"
SOURCE_ORDER_FILE = HERE / "source_order.txt"
BUILD_MK = HERE / "build.mk"
STARTUP_FILE = SDK_DIR / "boot" / "B85" / "cstartup_825x.S"
LAST_PROFILE_FILE = BUILD_ROOT / "last_profile.json"
CANONICAL_BIN = BUILD_ROOT / "825x_ble_sample.bin"
CANONICAL_MANIFEST = BUILD_ROOT / "fw_manifest.json"

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

# Numeric values intentionally match vendor/ble_sample/bms_board.h.
BOARD_PROFILES = {
    "hs-d011": {
        "define": 1,
        "label": "HS-D011-10S50A-V1",
        "default_afe": "sh3673510",
        "afes": ("mock", "sh3673510"),
    },
    "legacy-309": {
        "define": 2,
        "label": "TLSR8251-SH367309-LEGACY",
        "default_afe": "sh367309",
        "afes": ("mock", "sh367309"),
    },
}

AFE_MODELS = {
    "mock": {"define": 0, "mode": "SIMULATED"},
    "sh367309": {"define": 1, "mode": "REAL"},
    "sh3673510": {"define": 2, "mode": "REAL"},
}

BOARD_ALIASES = {
    "d011": "hs-d011",
    "hsd011": "hs-d011",
    "309": "legacy-309",
    "legacy309": "legacy-309",
}
AFE_ALIASES = {
    "sim": "mock",
    "simulation": "mock",
    "309": "sh367309",
    "3510": "sh3673510",
}
DEFAULT_BOARD = "legacy-309"

# Active artifact paths are configured after resolving a profile. Keeping each
# profile in a separate object directory prevents stale objects when CFLAGS
# change between AFE selections.
BUILD_DIR = BUILD_ROOT
OBJ_DIR = BUILD_DIR / "obj"
GEN_DIR = BUILD_DIR / "gen"
ELF = BUILD_DIR / "825x_ble_sample.elf"
RAW_BIN = BUILD_DIR / "825x_ble_sample.raw.bin"
BIN = BUILD_DIR / "825x_ble_sample.bin"
LST = GEN_DIR / "825x_ble_sample.lst"
MAP = GEN_DIR / "825x_ble_sample.map"
MANIFEST = BUILD_DIR / "fw_manifest.json"


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


def normalize_board(value: str) -> str:
    key = value.strip().lower()
    key = BOARD_ALIASES.get(key, key)
    if key not in BOARD_PROFILES:
        die(f"unknown board '{value}'. Run: python bms_tools/bms.py profiles")
    return key


def normalize_afe(value: str) -> str:
    key = value.strip().lower()
    key = AFE_ALIASES.get(key, key)
    if key not in AFE_MODELS:
        die(f"unknown AFE '{value}'. Run: python bms_tools/bms.py profiles")
    return key


def make_profile(board: str, afe: str | None = None) -> dict[str, object]:
    board = normalize_board(board)
    board_cfg = BOARD_PROFILES[board]
    afe = normalize_afe(afe or str(board_cfg["default_afe"]))
    allowed = tuple(board_cfg["afes"])
    if afe not in allowed:
        die(
            f"invalid board/AFE combination: {board} + {afe}; "
            f"allowed AFEs for {board}: {', '.join(allowed)}"
        )
    afe_cfg = AFE_MODELS[afe]
    return {
        "board": board,
        "board_define": int(board_cfg["define"]),
        "board_label": str(board_cfg["label"]),
        "afe": afe,
        "afe_define": int(afe_cfg["define"]),
        "afe_mode": str(afe_cfg["mode"]),
        "slug": f"{board}_{afe}",
    }


def resolve_profile(args: argparse.Namespace, prefer_last: bool = False) -> dict[str, object]:
    board_arg = getattr(args, "board", None)
    afe_arg = getattr(args, "afe", None)

    if board_arg is None and afe_arg is None and prefer_last:
        last = load_last_profile()
        if last is not None:
            return last

    board = normalize_board(board_arg) if board_arg is not None else DEFAULT_BOARD
    return make_profile(board, afe_arg)


def configure_artifact_paths(profile: dict[str, object]) -> None:
    global BUILD_DIR, OBJ_DIR, GEN_DIR, ELF, RAW_BIN, BIN, LST, MAP, MANIFEST
    BUILD_DIR = BUILD_ROOT / str(profile["slug"])
    OBJ_DIR = BUILD_DIR / "obj"
    GEN_DIR = BUILD_DIR / "gen"
    ELF = BUILD_DIR / "825x_ble_sample.elf"
    RAW_BIN = BUILD_DIR / "825x_ble_sample.raw.bin"
    BIN = BUILD_DIR / "825x_ble_sample.bin"
    LST = GEN_DIR / "825x_ble_sample.lst"
    MAP = GEN_DIR / "825x_ble_sample.map"
    MANIFEST = BUILD_DIR / "fw_manifest.json"


def load_last_profile() -> dict[str, object] | None:
    if not LAST_PROFILE_FILE.exists():
        return None
    try:
        data = json.loads(LAST_PROFILE_FILE.read_text(encoding="utf-8"))
        return make_profile(str(data["board"]), str(data["afe"]))
    except (OSError, ValueError, KeyError, TypeError):
        return None


def write_last_profile(profile: dict[str, object]) -> None:
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    data = {
        "board": profile["board"],
        "afe": profile["afe"],
        "afe_mode": profile["afe_mode"],
        "build_dir": rel(BUILD_DIR),
    }
    LAST_PROFILE_FILE.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def print_profile(profile: dict[str, object], prefix: str = "") -> None:
    print(f"{prefix}target MCU : {TARGET_MCU}")
    print(f"{prefix}board      : {profile['board']} ({profile['board_label']})")
    print(f"{prefix}AFE        : {profile['afe']}")
    print(f"{prefix}AFE mode   : {profile['afe_mode']}")


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
    if "-DBMS_BOARD_PROFILE=$(BMS_BOARD_PROFILE)" not in build_text:
        die("build.mk does not forward BMS_BOARD_PROFILE to the compiler")
    if "-DBMS_AFE_MODEL=$(BMS_AFE_MODEL)" not in build_text:
        die("build.mk does not forward BMS_AFE_MODEL to the compiler")

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
        "# TLSR8251 BMS authoritative source/link order.\n"
        "# Board/AFE selection is compile-time; object order remains deterministic.\n"
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


def run_make(jobs: int, profile: dict[str, object]) -> None:
    validate_target_contract()
    order = validate_source_order()
    generate_sources_mk(order)
    env = toolchain_env()

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
        f"BUILD_DIR={rel(BUILD_DIR)}",
        f"BMS_BOARD_PROFILE={profile['board_define']}",
        f"BMS_AFE_MODEL={profile['afe_define']}",
        "all",
    ]
    info(f"starting TC32 build without Telink IDE ({TARGET_MCU}, {STARTUP_PROFILE})")
    info(f"board={profile['board']}  afe={profile['afe']}  mode={profile['afe_mode']}")
    result = subprocess.run(cmd, cwd=REPO_ROOT, env=env, check=False)
    if result.returncode != 0:
        die(f"build failed, make exit code={result.returncode}", result.returncode)


def firmware_checker() -> Path:
    return TL_CHECK_WIN if os.name == "nt" else TL_CHECK_LINUX


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

    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    shutil.copy2(BIN, CANONICAL_BIN)


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


def selected_config_inputs() -> list[Path]:
    base = SDK_DIR / "vendor" / "ble_sample"
    return [
        base / "bms_board.h",
        base / "board_legacy_309.h",
        base / "board_hs_d011.h",
        base / "bms_afe.h",
        base / "app_config.h",
    ]


def write_manifest(profile: dict[str, object]) -> None:
    if not BIN.exists() or not ELF.exists():
        die("build artifacts missing; run build/rebuild first")
    order = validate_source_order(verbose=False)
    inputs = [BUILD_MK, SOURCE_ORDER_FILE, LINKER_FILE, STARTUP_FILE, *VENDOR_LIBS, *selected_config_inputs()]
    source_inputs = {
        rel(SDK_DIR / p): sha256(SDK_DIR / p)
        for p in order
        if (SDK_DIR / p).exists()
    }
    manifest = {
        "format": 3,
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "git_head": git_head(),
        "target": f"{TARGET_MCU} / B85 / 825x_ble_sample",
        "startup_profile": STARTUP_PROFILE,
        "build_profile": {
            "board": profile["board"],
            "board_label": profile["board_label"],
            "board_define": profile["board_define"],
            "afe": profile["afe"],
            "afe_define": profile["afe_define"],
            "afe_mode": profile["afe_mode"],
        },
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
            "canonical_bin": {
                "path": rel(CANONICAL_BIN),
                "size": CANONICAL_BIN.stat().st_size,
                "sha256": sha256(CANONICAL_BIN),
            },
        },
        "build_inputs": {rel(p): sha256(p) for p in inputs if p.exists()},
        "source_inputs": source_inputs,
    }
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    shutil.copy2(MANIFEST, CANONICAL_MANIFEST)
    write_last_profile(profile)
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
        if sha256(p) != item["sha256"]:
            failures.append(f"{name}: sha256 mismatch")
    for section in ("build_inputs", "source_inputs"):
        for path_text, expected in data.get(section, {}).items():
            p = REPO_ROOT / path_text
            if not p.exists() or sha256(p) != expected:
                failures.append(f"{section[:-1]} changed: {path_text}")
    if failures:
        die("manifest verification failed:\n  " + "\n  ".join(failures))
    profile = data.get("build_profile", {})
    if profile:
        info(
            f"manifest profile: board={profile.get('board')} "
            f"afe={profile.get('afe')} mode={profile.get('afe_mode')}"
        )
    info("manifest verification PASS")


def cmd_profiles(_: argparse.Namespace) -> int:
    print("Available boards / valid AFE combinations:")
    for board, cfg in BOARD_PROFILES.items():
        print(f"  {board:<12} default={cfg['default_afe']:<10} allowed={', '.join(cfg['afes'])}")
    print("\nAFE modes:")
    for afe, cfg in AFE_MODELS.items():
        print(f"  {afe:<10} {cfg['mode']}")
    print("\nDefault build when --board/--afe are omitted:")
    print_profile(make_profile(DEFAULT_BOARD), prefix="  ")
    return 0


def cmd_env(args: argparse.Namespace) -> int:
    profile = resolve_profile(args)
    configure_artifact_paths(profile)
    env = toolchain_env()
    validate_target_contract()
    print_profile(profile)
    print(f"repo              : {REPO_ROOT}")
    print(f"sdk               : {SDK_DIR} (exists={SDK_DIR.exists()})")
    print(f"linker            : {LINKER_FILE} (exists={LINKER_FILE.exists()})")
    print(f"startup           : {STARTUP_FILE} (exists={STARTUP_FILE.exists()})")
    print(f"build output      : {BUILD_DIR}")
    print(f"canonical BIN     : {CANONICAL_BIN}")
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


def do_build(args: argparse.Namespace, clean: bool) -> None:
    profile = resolve_profile(args)
    configure_artifact_paths(profile)
    if clean and BUILD_DIR.exists():
        info(f"removing previous profile output: {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)
    run_make(args.jobs, profile)
    validate_map_sram()
    finalize_firmware()
    write_manifest(profile)
    info(f"ELF : {ELF}")
    info(f"BIN : {BIN}")
    info(f"latest burn BIN: {CANONICAL_BIN}")
    info(f"MAP : {MAP}")
    info(f"LST : {LST}")


def cmd_build(args: argparse.Namespace) -> int:
    do_build(args, clean=False)
    return 0


def cmd_rebuild(args: argparse.Namespace) -> int:
    do_build(args, clean=True)
    return 0


def select_existing_artifacts(args: argparse.Namespace) -> dict[str, object]:
    profile = resolve_profile(args, prefer_last=True)
    configure_artifact_paths(profile)
    return profile


def cmd_check_fw(args: argparse.Namespace) -> int:
    profile = select_existing_artifacts(args)
    if not RAW_BIN.exists():
        die(f"raw BIN missing for {profile['board']} + {profile['afe']}; run build/rebuild first")
    validate_map_sram()
    finalize_firmware()
    write_manifest(profile)
    info("official firmware post-check PASS")
    return 0


def cmd_size(args: argparse.Namespace) -> int:
    select_existing_artifacts(args)
    if not ELF.exists():
        die("ELF missing; run build/rebuild first")
    env = toolchain_env()
    tool = find_tool("tc32-elf-size", env)
    return subprocess.run([tool, "-t", str(ELF)], env=env, check=False).returncode


def cmd_map(args: argparse.Namespace) -> int:
    profile = select_existing_artifacts(args)
    if not MAP.exists():
        die("MAP missing; run build/rebuild first")
    print(f"Profile: board={profile['board']} afe={profile['afe']} mode={profile['afe_mode']}")
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


def cmd_manifest(args: argparse.Namespace) -> int:
    profile = select_existing_artifacts(args)
    write_manifest(profile)
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    select_existing_artifacts(args)
    verify_manifest()
    return 0


def cmd_ci(args: argparse.Namespace) -> int:
    do_build(args, clean=True)
    verify_manifest()
    return 0


def add_profile_args(s: argparse.ArgumentParser) -> None:
    s.add_argument(
        "--board",
        metavar="NAME",
        help="board profile (legacy-309, hs-d011); aliases: 309, d011",
    )
    s.add_argument(
        "--afe",
        metavar="NAME",
        help="AFE backend (mock, sh367309, sh3673510); aliases: sim, 309, 3510",
    )


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="TLSR8251 multi-board/multi-AFE no-IDE build tooling")
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("profiles", help="list valid board/AFE build combinations")
    s.set_defaults(func=cmd_profiles)

    s = sub.add_parser("env", help="check local compiler/build environment")
    add_profile_args(s)
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
        add_profile_args(s)
        s.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
        s.set_defaults(func=func)

    for name, func, help_text in (
        ("check-fw", cmd_check_fw, "regenerate canonical BIN and run Telink checker"),
        ("size", cmd_size, "print TC32 ELF size"),
        ("map", cmd_map, "show key MAP symbols and TLSR8251 SRAM margin"),
        ("manifest", cmd_manifest, "write build integrity manifest"),
        ("verify", cmd_verify, "verify artifacts/build inputs against manifest"),
    ):
        s = sub.add_parser(name, help=help_text)
        add_profile_args(s)
        s.set_defaults(func=func)
    return p


def main() -> int:
    args = parser().parse_args()
    return int(args.func(args) or 0)


if __name__ == "__main__":
    raise SystemExit(main())
