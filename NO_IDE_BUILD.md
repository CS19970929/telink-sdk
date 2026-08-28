# TLSR8251 multi-board command-line build (no Telink IDE)

This branch builds `tc_ble_single_sdk/vendor/ble_sample` without opening Telink IDE. The CLI keeps the Telink SDK compiler ABI, linker script, prebuilt libraries and official firmware post-processing, while selecting the BMS board and AFE backend explicitly at compile time.

## Build profiles

List supported combinations:

```powershell
python bms_tools/bms.py profiles
```

Current combinations:

```text
legacy-309 + sh367309   REAL
legacy-309 + mock       SIMULATED
hs-d011    + sh3673510  REAL
hs-d011    + mock       SIMULATED
```

The default, when `--board` and `--afe` are omitted, is `legacy-309 + sh367309`, because that is the current real-board validation target.

Examples:

```powershell
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
python bms_tools/bms.py rebuild --board legacy-309 --afe mock --jobs 4
python bms_tools/bms.py rebuild --board hs-d011 --afe sh3673510 --jobs 4
python bms_tools/bms.py rebuild --board hs-d011 --afe mock --jobs 4
```

The CLI validates the board/AFE pair before Make is invoked. The selected values are forwarded as compiler defines:

```text
BMS_BOARD_PROFILE
BMS_AFE_MODEL
```

No C header needs to be edited to switch an AFE.

## Artifact isolation

Each board/AFE pair gets a separate object/output directory, for example:

```text
tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/
├─ legacy-309_sh367309/
├─ legacy-309_mock/
├─ hs-d011_sh3673510/
└─ hs-d011_mock/
```

This is required for reliable incremental builds: Make otherwise cannot know that an object compiled with one `-DBMS_AFE_MODEL=...` must be rebuilt after the AFE selection changes.

After a successful Telink firmware post-check, the selected profile's BIN is also copied to the stable burn path:

```text
tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

`last_profile.json` records which profile produced that canonical BIN. `map`, `size`, `check-fw`, `manifest` and `verify` use the last successful profile when no explicit `--board/--afe` is supplied.

## Manifest

The profile manifest records:

```text
MCU
startup profile
board name + numeric define
AFE name + numeric define
REAL/SIMULATED mode
source order hash
source file hashes
critical build/configuration input hashes
ELF/BIN/raw BIN hashes
canonical BIN hash
```

Therefore `python bms_tools/bms.py verify` checks not only the generated BIN but also whether source or profile-defining inputs changed after the build.

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

`__PROJECT_8258_BLE_SAMPLE__` remains the SDK application-selection macro. It is not the SRAM-size selector. `cstartup_825x.S` uses `MCU_STARTUP_8251` for the actual TLSR8251 SRAM contract.

```text
SRAM base            0x840000
SRAM size            32 KiB
SRAM end/exclusive   0x848000
main SP after reset  0x848000
```

`boot.link` enforces the TLSR8251 SRAM profile and the main-stack guard. `bms.py` also performs the post-link SRAM/MAP check.

## Windows prerequisites

The build script searches for the TC32 toolchain in this order:

1. `TC32_TOOLCHAIN_BIN`;
2. current `PATH`;
3. `C:\\TelinkIoTStudio\\opt\\tc32\\bin`.

GNU Make is searched from `MAKE`, then `PATH`, then `C:\\qp\\qtools\\bin\\make.exe`.

Example:

```powershell
$env:TC32_TOOLCHAIN_BIN = 'C:\TelinkIoTStudio\opt\tc32\bin'
python bms_tools/bms.py env --board legacy-309 --afe sh367309
```

## Recommended real-309 workflow

```powershell
git pull
python bms_tools/bms.py profiles
python bms_tools/bms.py env --board legacy-309 --afe sh367309
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
python bms_tools/bms.py map
python bms_tools/bms.py verify
```

Use the top-level `825x_ble_sample_cli/825x_ble_sample.bin` as the latest successfully post-checked burn image.

The root Makefile exposes the same selection:

```powershell
make profiles
make rebuild BOARD=legacy-309 AFE=sh367309 JOBS=4
make rebuild BOARD=hs-d011 AFE=sh3673510 JOBS=4
```

## Source/link order

`bms_tools/source_order.txt` is version controlled. Build/rebuild refuses to continue if a managed `.c` or `.S` is added, removed or renamed without updating the list.

After intentionally adding a source file:

```powershell
python bms_tools/bms.py sources --update
git diff -- bms_tools/source_order.txt
python bms_tools/bms.py rebuild --board legacy-309 --afe sh367309 --jobs 4
```

Review the order diff before committing. This keeps link layout deterministic.
