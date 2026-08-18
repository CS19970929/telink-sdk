# Telink 通用 BMS 平台

这是 TLSR8251/TLSR825x 上的新一代 BMS 母模板。首版产品默认配置为 SH3673520、20S、同口功率路径；这些是产品默认值，不是协议或核心数据模型的固定上限。

## 当前范围

- 运行时数据模型：动态 cell_count/temperature_count、静态上限数组、统一单位和有效位；
- AFE 边界：以采样、均衡、逻辑功率路径、硬件故障和低功耗能力抽象；
- SH36735：仅实现 SPI/CRC/寄存器访问边界，尚未接入板级 SPI、RSENSE、NTC 曲线和 MOS 引脚；
- 目标工具链：Telink TC32 GCC 4.5.1-tc32-1.3，不使用 ARM GCC；
- BMSLink v1、BLE/GATT、参数 Schema、基础保护/SOC/均衡/加热策略和官方 OTA 服务均已接入；实际硬件测量、标定和量产安全配置仍需板级验证。

## 构建与静态分析

在本目录运行：

    python tools/bms.py env
    python tools/bms.py build-core
    python tools/bms.py build-firmware
    python tools/bms.py static
    python tools/bms.py test

build-core 只编译本平台的业务核心和 AFE 边界，作为快速 TC32 语法与警告门禁。build-firmware 构建 BMS 自有 BLE 外壳、SDK 公共运行时、`MCU_STARTUP_8251` 启动文件和 825x 协议栈，并执行 SDK 固件检查；它仍须经过实机验证后才可刷写到产品板。

## 目录

    board/       产品默认配置和后续板级适配
    core/        BMS 数据模型及应用协调器
    afe/         通用 AFE 接口与芯片实现
    include/     对内稳定头文件
    schema/      PC/App 使用的版本化参数公开清单
    docs/        架构、构建和决策记录
    tools/       独立 TC32/Cppcheck 入口

详细边界见 docs/architecture.md 和 docs/build.md。
