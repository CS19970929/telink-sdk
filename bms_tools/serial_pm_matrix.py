#!/usr/bin/env python3
"""Build the serial low-power A/B-test firmware matrix in one command.

Default target is the currently validated legacy TLSR8251 + SH367309 board.
Each variant performs a clean rebuild so compiler defines cannot leak between
outputs. The stable/current variant is built last, leaving the canonical burn
BIN on the normal validated behavior after the matrix command completes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
BMS_CLI = HERE / "bms.py"
BUILD_ROOT = (
    REPO_ROOT
    / "tc_ble_single_sdk"
    / "project"
    / "tlsr_tc32"
    / "B85"
    / "825x_ble_sample_cli"
)

BOARD_ALIASES = {
    "309": "legacy-309",
    "legacy309": "legacy-309",
    "d011": "hs-d011",
    "hsd011": "hs-d011",
}
AFE_ALIASES = {
    "309": "sh367309",
    "3510": "sh3673510",
    "sim": "mock",
    "simulation": "mock",
}
DEFAULT_AFE = {
    "legacy-309": "sh367309",
    "hs-d011": "sh3673510",
}

# Build diagnostic variants first and CURRENT last. CURRENT therefore remains
# the top-level canonical 825x_ble_sample.bin after this helper exits.
VARIANTS_BUILD_ORDER = (
    {
        "tag": "D",
        "name": "no-wake-hiz",
        "define": 3,
        "pad": False,
        "risc0": False,
        "tx_hiz": True,
        "serial_wake_expected": False,
        "purpose": "Power reference: UART sleeps after 3 s but serial cannot wake it.",
    },
    {
        "tag": "C",
        "name": "pad-hiz",
        "define": 2,
        "pad": True,
        "risc0": False,
        "tx_hiz": True,
        "serial_wake_expected": True,
        "purpose": "Candidate: PAD low wake only, TX high-Z.",
    },
    {
        "tag": "B",
        "name": "dual-hiz",
        "define": 1,
        "pad": True,
        "risc0": True,
        "tx_hiz": True,
        "serial_wake_expected": True,
        "purpose": "Isolates TX-high leakage from the current dual-wake design.",
    },
    {
        "tag": "A",
        "name": "current-dual-txhigh",
        "define": 0,
        "pad": True,
        "risc0": True,
        "tx_hiz": False,
        "serial_wake_expected": True,
        "purpose": "Current bench-validated behavior; built last and left canonical.",
    },
)


def normalize_board(value: str) -> str:
    key = BOARD_ALIASES.get(value.strip().lower(), value.strip().lower())
    if key not in DEFAULT_AFE:
        raise SystemExit(f"unknown board: {value}")
    return key


def normalize_afe(value: str) -> str:
    return AFE_ALIASES.get(value.strip().lower(), value.strip().lower())


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def git_head() -> str | None:
    try:
        r = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        return r.stdout.strip() if r.returncode == 0 else None
    except OSError:
        return None


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Clean-build four serial PM firmware variants for power A/B testing"
    )
    p.add_argument("--board", default="legacy-309", help="default: legacy-309; alias: 309")
    p.add_argument("--afe", default=None, help="default follows board; alias: 309/3510")
    p.add_argument("--jobs", type=int, default=1, help="parallel make jobs per variant (default: 1)")
    return p


def main() -> int:
    args = parser().parse_args()
    board = normalize_board(args.board)
    afe = normalize_afe(args.afe) if args.afe else DEFAULT_AFE[board]
    jobs = max(1, args.jobs)

    profile_slug = f"{board}_{afe}"
    profile_dir = BUILD_ROOT / profile_slug
    source_bin = profile_dir / "825x_ble_sample.bin"
    source_manifest = profile_dir / "fw_manifest.json"
    matrix_dir = BUILD_ROOT / "serial_pm_matrix" / profile_slug

    if matrix_dir.exists():
        shutil.rmtree(matrix_dir)
    matrix_dir.mkdir(parents=True, exist_ok=True)

    outputs: list[dict[str, object]] = []
    print(f"[serial-pm-matrix] target: {board} + {afe}")
    print("[serial-pm-matrix] clean-building 4 variants; CURRENT is built last")

    for variant in VARIANTS_BUILD_ORDER:
        name = str(variant["name"])
        define = int(variant["define"])
        print(f"\n[serial-pm-matrix] === {variant['tag']} {name} (define={define}) ===")

        env = dict(os.environ)
        env["BMS_SERIAL_PM_VARIANT"] = str(define)
        cmd = [
            sys.executable,
            str(BMS_CLI),
            "rebuild",
            "--board",
            board,
            "--afe",
            afe,
            "--jobs",
            str(jobs),
        ]
        result = subprocess.run(cmd, cwd=REPO_ROOT, env=env, check=False)
        if result.returncode != 0:
            print(
                f"[serial-pm-matrix] ERROR: {name} build failed, exit={result.returncode}",
                file=sys.stderr,
            )
            return result.returncode

        if not source_bin.exists():
            print(f"[serial-pm-matrix] ERROR: missing {source_bin}", file=sys.stderr)
            return 2

        dst_bin = matrix_dir / f"{variant['tag']}_{name}.bin"
        shutil.copy2(source_bin, dst_bin)

        dst_manifest = None
        if source_manifest.exists():
            dst_manifest = matrix_dir / f"{variant['tag']}_{name}.manifest.json"
            shutil.copy2(source_manifest, dst_manifest)

        outputs.append(
            {
                **variant,
                "bin": dst_bin.relative_to(REPO_ROOT).as_posix(),
                "bin_size": dst_bin.stat().st_size,
                "bin_sha256": sha256(dst_bin),
                "build_manifest": (
                    dst_manifest.relative_to(REPO_ROOT).as_posix() if dst_manifest else None
                ),
            }
        )

    matrix = {
        "format": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "git_head": git_head(),
        "board": board,
        "afe": afe,
        "idle_sleep_ms": 3000,
        "sleep_guard_ms": 50,
        "diagnostics": {
            "D148": "variant id: 0=current, 1=dual-hiz, 2=pad-hiz, 3=no-wake-hiz",
            "D149": "bit0=PAD wake, bit1=RISC0 wake, bit2=TX sleep high-Z",
        },
        "variants": sorted(outputs, key=lambda item: str(item["tag"])),
        "canonical_after_command": "A/current variant (define=0)",
    }
    matrix_json = matrix_dir / "matrix.json"
    matrix_json.write_text(json.dumps(matrix, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print("\n[serial-pm-matrix] PASS: all variants built and post-checked")
    print(f"[serial-pm-matrix] output: {matrix_dir}")
    print("[serial-pm-matrix] flash/test in this order:")
    for item in sorted(outputs, key=lambda x: str(x["tag"])):
        wake = "wake expected" if item["serial_wake_expected"] else "NO serial wake (power reference)"
        print(f"  {item['tag']}: {item['bin']}  [{wake}]")
    print("[serial-pm-matrix] canonical 825x_ble_sample.bin is left on A/current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
