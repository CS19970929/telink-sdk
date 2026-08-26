# 构建与质量门禁

## 固定环境

本项目使用 SDK 已验证的独立工具链，不调用 IDE：

- 编译器：`C:\TelinkIoTStudio\opt\tc32\bin\tc32-elf-gcc.exe`，TC32 GCC 4.5.1-tc32-1.3；
- 静态检查：`C:\Program Files\cppcheck\cppcheck.exe`；
- SDK 库：`proj_lib/liblt_825x.a` 与 `proj_lib/liblt_general_stack.a`。

从 `tc_ble_single_sdk/vendor/bms_platform` 目录执行：

    python tools/bms.py env
    python tools/bms.py build-core
    python tools/bms.py build-firmware
    python tools/bms.py build-lab-firmware
    python tools/bms.py build-lab-ota-firmware
    python tools/bms.py build-lab-ota-proof-firmware
    python tools/bms.py static

`env` 验证工具与 SDK 必需库。`build-core` 使用 TC32 对 BMS 自有源码逐一编译，启用 `-Wall -Wextra -Werror`，并生成 `build/tc32/build_manifest.json`。`build-firmware` 显式以 `MCU_STARTUP_8251=1` 汇编启动文件、链接 `liblt_825x.a`，并编译该 SDK 对应的 `common/`、`drivers/B85/`、`vendor/common/` 和 `application/print/` 运行时源码，生成 elf/bin 后调用 SDK `tl_check_fw2.exe` 检查镜像。BMS 自有源码仍以零警告要求编译；官方 SDK 运行时保留其原有的警告策略。`static` 运行 Cppcheck 并生成 `build/static/cppcheck.xml`。
`test` 运行不依赖目标板的协议固定向量；该命令禁用 Python 字节码缓存，避免在工作树产生临时文件。

## 本阶段边界

`build-core` 是快速质量门禁。`build-firmware` 会生成已检查的 bin，但 BMS 的实际引脚和 AFE 标定尚未确认，因而它不是批准刷写到产品板的充分条件。完整硬件验证必须采用 `MCU_STARTUP_8251` 和本 SDK 的 825x 库/链接脚本；禁止照搬旧工程中的 `MCU_STARTUP_8258`。

## 官方开发板实验室镜像

`build-lab-firmware` 是给官方 TLSR8251 开发板使用的专用镜像，产物为 `build/lab_firmware/telink_bms.bin`。它每 500 ms 发布确定性的 20S、4 路温度、总压、电流、充/放电状态模拟值，用于验证广播、BLE 分片、BMSLink、PC/App、参数、SOC、保护和事件路径。它不会访问 AFE SPI，也不会配置或驱动 AFE/MOS 控制 GPIO；唯一的通用板级输出是 PC2 的 1 Hz 心跳。不能用于真实电池包。

所有固件构建默认启用 SDK 参考 OTA 布局（124 KiB 镜像、`0x20000` 次镜像槽）。普通固件直接执行 `python tools/bms.py build-firmware`，产物为 `build/firmware/telink_bms.bin`；实验室固件也自动带 OTA。目标板必须确认 Flash 容量、分区和断电恢复流程后才能实际刷写。

`build-lab-ota-proof-firmware` 使用与前者相同的受限布局和模拟数据，但设备信息版本为 `0.2.1`，产物为 `build/lab_ota_proof_firmware/telink_bms.bin`。把 `0.2.0` 基准镜像用下载器刷入后，使用 GUI OTA 页传输该镜像；重启重连后版本变为 `0.2.1` 才能证明 Bootloader 实际启动了 OTA 新镜像。

`build-lab-ota-control-firmware` 使用同一受限 OTA 布局，版本为 `0.2.2`，产物为 `build/lab_ota_control_firmware/telink_bms.bin`。它在 `0.2.1` OTA 验证镜像基础上增加 BMSLink 蓝牙名称控制，配合新版 PC 上位机的 1 秒自动刷新和中文参数表使用。它同时启用双槽 CRC 配置持久化：两个 4 KiB 扇区固定为 `0x72000`、`0x73000`，不重叠 OTA 最大区域和 SDK 的 SMP/MAC/校准扇区；仅当启动时检测到 512 KiB Flash 才开放参数和名称写入。

`build/` 是正式、可复现的本地构建输出，但不进入版本控制。工具不会在工作树创建临时目录；宿主工具产生的中间状态使用系统临时区。

## 变更规则

当在 `board/`、`core/` 或 `afe/` 新增或删除 C 文件时，同步更新 `tools/bms.py` 中的 `EXPECTED_SOURCES`。该显式清单让构建遗漏源码时立即失败，而不是产生不完整的测试通过结果。
