# Telink 通用 BMS 平台

这是 TLSR8251/TLSR825x 上的新一代 BMS 母模板。首版产品默认配置为 SH3673520、20S、同口功率路径；这些是产品默认值，不是协议或核心数据模型的固定上限。

## 当前范围

- 运行时数据模型：动态 cell_count/temperature_count、静态上限数组、统一单位和有效位；
- AFE 边界：以采样、均衡、逻辑功率路径、硬件故障和低功耗能力抽象；
- SH36735：已实现 SPI/CRC、原始快照、串数、均衡、MOS、模式和故障事务；工程量换算与实际控制仍由板级回调和显式安全许可决定；
- 目标工具链：Telink TC32 GCC 4.5.1-tc32-1.3，不使用 ARM GCC；
- BMSLink v1、BLE/GATT、参数 Schema、基础保护/SOC/均衡/加热策略和官方 OTA 服务均已接入；PC 端具备官方 legacy OTA 传输与结果确认，但默认以板级 Flash 布局门禁安全禁用；实际硬件测量、标定和量产安全配置仍需板级验证。

## 构建与静态分析

在本目录运行：

    python tools/bms.py env
    python tools/bms.py build-core
    python tools/bms.py build-firmware
    python tools/bms.py build-lab-firmware
    python tools/bms.py build-pc-exe
    python tools/bms.py static
    python tools/bms.py test

build-core 只编译本平台的业务核心和 AFE 边界，作为快速 TC32 语法与警告门禁。build-firmware 构建 BMS 自有 BLE 外壳、SDK 公共运行时、`MCU_STARTUP_8251` 启动文件和 825x 协议栈，并执行 SDK 固件检查；它仍须经过实机验证后才可刷写到产品板。

没有 AFE 时，可用官方 TLSR8251 开发板执行 `build-lab-firmware`，得到不触碰 AFE/SPI/MOS 的确定性模拟数据 BLE 验证镜像；需要测试 OTA 时另用受 Flash 容量条件保护的 `build-lab-ota-firmware`。完整流程见 `docs/lab_testing.md`。

Windows 图形上位机已构建为 `build/pc_client/TelinkBMS.exe`，可直接双击运行；后续需要重新打包时使用 `build-pc-exe`。完整说明见 `docs/pc_client.md`。

## 目录

    board/       产品默认配置和后续板级适配
    core/        BMS 数据模型及应用协调器
    afe/         通用 AFE 接口与芯片实现
    include/     对内稳定头文件
    schema/      PC/App 使用的版本化参数公开清单
    pc_client/   Tk 桌面上位机、CLI、BMSLink/BLE Transport
    mobile_app/  Flutter Android/iOS BMSLink 客户端与平台权限片段
    docs/        架构、构建和决策记录
    tools/       独立 TC32/Cppcheck 入口

详细边界见 docs/architecture.md、docs/sh36735.md、docs/afe_replacement_review.md、docs/build.md、docs/lab_testing.md、docs/parameters.md、docs/pc_client.md 和 docs/mobile_app.md。
