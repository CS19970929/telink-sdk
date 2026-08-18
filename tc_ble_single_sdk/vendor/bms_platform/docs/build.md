# 构建与质量门禁

## 固定环境

本项目使用 SDK 已验证的独立工具链，不调用 IDE：

- 编译器：`C:\TelinkIoTStudio\opt\tc32\bin\tc32-elf-gcc.exe`，TC32 GCC 4.5.1-tc32-1.3；
- 静态检查：`C:\Program Files\cppcheck\cppcheck.exe`；
- SDK 库：`proj_lib/liblt_825x.a` 与 `proj_lib/liblt_general_stack.a`。

从 `tc_ble_single_sdk/vendor/bms_platform` 目录执行：

    python tools/bms.py env
    python tools/bms.py build-core
    python tools/bms.py static

`env` 验证工具与 SDK 必需库。`build-core` 使用 TC32 对 BMS 自有源码逐一编译，启用 `-Wall -Wextra -Werror`，并生成 `build/tc32/build_manifest.json`。`static` 运行 Cppcheck 并生成 `build/static/cppcheck.xml`。

## 本阶段边界

这些命令是 BMS 核心和 AFE 边界的可重复质量门禁，尚不链接协议栈、不生成 bin，也不能刷写。完整固件进入阶段 4 后，构建脚本必须显式采用 `MCU_STARTUP_8251` 和本 SDK 的 825x 库/链接脚本，并在真实 TLSR8251 硬件上验证；禁止照搬旧工程中的 `MCU_STARTUP_8258`。

`build/` 是正式、可复现的本地构建输出，但不进入版本控制。工具不会在工作树创建临时目录；宿主工具产生的中间状态使用系统临时区。

## 变更规则

当在 `board/`、`core/` 或 `afe/` 新增或删除 C 文件时，同步更新 `tools/bms.py` 中的 `EXPECTED_SOURCES`。该显式清单让构建遗漏源码时立即失败，而不是产生不完整的测试通过结果。
