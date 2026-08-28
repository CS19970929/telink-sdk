# HS-D011 command-line build (no Telink IDE)

This branch can build `tc_ble_single_sdk/vendor/ble_sample` without opening Telink IDE. The command-line build keeps the current Telink SDK compiler ABI, linker script, prebuilt libraries and firmware post-processing rather than switching to the Ai-Thinker SDK layout.

## Build contract

The command-line build uses:

- application-selection macro: `__PROJECT_8258_BLE_SAMPLE__=1`;
- family macro: `CHIP_TYPE=CHIP_TYPE_825x`;
- optimization: `-O2`;
- language/ABI flags: `gnu99`, `-fpack-struct`, `-fshort-enums`, `-fshort-wchar`, `-fms-extensions`, `-finline-small-functions`;
- linker: `tc32-elf-ld --gc-sections`;
- linker script: `tc_ble_single_sdk/project/tlsr_tc32/B85/boot.link`;
- libraries: `liblt_825x.a` and `liblt_general_stack.a`;
- startup assembler macro: `MCU_STARTUP_8251`;
- final firmware post-processing: SDK `tl_check_fw2.exe` on Windows or `check_fw` on Linux.

`__PROJECT_8258_BLE_SAMPLE__` is retained because it selects the SDK's BLE sample application configuration. It is not the SRAM-size selector. The actual silicon/SRAM selection is performed in `cstartup_825x.S` by `MCU_STARTUP_8251`.

For TLSR8251 the SDK startup file defines:

```text
SRAM base            0x840000
SRAM size            32 KiB
SRAM end/exclusive   0x848000
main SP after reset  0x848000
```

The old Eclipse configuration used `MCU_STARTUP_8258`, which selected a 64 KiB end address `0x850000`. That would place the main stack pointer outside a TLSR8251's 32 KiB SRAM. The CLI build therefore intentionally differs from that old startup setting while keeping the rest of the verified B85 build contract.

`boot.link` already contains a RAM safety assertion equivalent to:

```text
_ram_use_end_ < (__SRAM_SIZE - 600)
```

With `MCU_STARTUP_8251`, `__SRAM_SIZE` becomes `0x848000`. `bms.py` additionally checks the startup/build contract and reports MAP SRAM headroom after linking.

## Windows prerequisites

The easiest path is to keep the TC32 compiler already installed by Telink IoT Studio; the IDE GUI itself is not used.

The build script searches in this order:

1. `TC32_TOOLCHAIN_BIN` environment variable;
2. current `PATH`;
3. `C:\\TelinkIoTStudio\\opt\\tc32\\bin`.

GNU Make is searched from `MAKE`, then `PATH`, then `C:\\qp\\qtools\\bin\\make.exe`.

Example PowerShell override:

```powershell
$env:TC32_TOOLCHAIN_BIN = 'C:\TelinkIoTStudio\opt\tc32\bin'
python bms_tools/bms.py env
```

## Build and verify

From the repository root:

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 1
python bms_tools/bms.py map
python bms_tools/bms.py verify
```

A successful build produces:

```text
tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/
├─ 825x_ble_sample.elf
├─ 825x_ble_sample.raw.bin
├─ 825x_ble_sample.bin
├─ fw_manifest.json
├─ gen/
│  ├─ 825x_ble_sample.map
│  └─ 825x_ble_sample.lst
└─ obj/
```

Use **`825x_ble_sample.bin`** for the canonical post-processed firmware. `825x_ble_sample.raw.bin` is an intermediate objcopy image.

Additional commands:

```powershell
python bms_tools/bms.py build --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py ci --jobs 4
```

`ci` means clean rebuild + TLSR8251 SRAM/MAP check + official firmware post-check + manifest verification. Board-level tests remain separate.

## Source/link order

`bms_tools/source_order.txt` is version controlled. Build/rebuild refuses to continue if a managed `.c` or `.S` is added, removed or renamed without updating the list.

After intentionally adding a source file:

```powershell
python bms_tools/bms.py sources --update
git diff -- bms_tools/source_order.txt
python bms_tools/bms.py rebuild --jobs 4
```

Review the order diff before committing. This keeps link layout deterministic and avoids silently changing firmware layout because of filesystem sorting.

## Why this does not use the Ai-Thinker build files directly

Ai-Thinker's repository is useful as a reference for Linux/WSL and CI, but its firmware model differs from this SDK: it links `liblt_8258.a`, uses a different linker layout (application text at `0x4000` for its UART bootloader model), and uses its own firmware tooling. This project therefore keeps the Telink V3.4.2.8 Patch 0001 `boot.link`, `liblt_825x.a`, `liblt_general_stack.a`, and official SDK post-processing.
