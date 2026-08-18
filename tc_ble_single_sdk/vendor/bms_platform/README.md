# Telink 通用 BMS 平台

这是 TLSR8251/TLSR825x 上的新一代 BMS 母模板。首版产品默认配置为 SH3673520、20S、同口功率路径；这些是产品默认值，不是协议或核心数据模型的固定上限。

## 当前范围

- 运行时数据模型：动态 cell_count/temperature_count、静态上限数组、统一单位和有效位；
- AFE 边界：以采样、均衡、逻辑功率路径、硬件故障和低功耗能力抽象；
- SH36735：仅实现 SPI/CRC/寄存器访问边界，尚未接入板级 SPI、RSENSE、NTC 曲线和 MOS 引脚；
- 目标工具链：Telink TC32 GCC 4.5.1-tc32-1.3，不使用 ARM GCC；
- BMSLink v1 编解码器已就绪；BLE、参数、保护和 OTA 将在后续独立提交中实现。

## 构建与静态分析

在本目录运行：

    python tools/bms.py env
    python tools/bms.py build-core
    python tools/bms.py static
    python tools/bms.py test

build-core 只编译本平台的业务核心和 AFE 边界，作为 Phase 0/1 的 TC32 语法与警告门禁；它不是可烧录 BLE 固件。完整 BLE Firmware 构建会在移植 ble_sample 的最小启动、PM 和 GATT 骨架后加入。

## 目录

    board/       产品默认配置和后续板级适配
    core/        BMS 数据模型及应用协调器
    afe/         通用 AFE 接口与芯片实现
    include/     对内稳定头文件
    docs/        架构、构建和决策记录
    tools/       独立 TC32/Cppcheck 入口

详细边界见 docs/architecture.md 和 docs/build.md。
