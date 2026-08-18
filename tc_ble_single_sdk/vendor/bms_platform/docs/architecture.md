# BMS 平台架构

## 目标与默认产品

平台以 TLSR8251/TLSR825x 为 MCU 平台，采用可替换 AFE 的分层设计。首个产品默认值为 SH3673520、20 串、4 路温度、同口功率路径。数据模型的编译期容量为 32 串与 8 路温度；运行时有效数量由 `BmsProductConfig` 提供，因此后续产品不需要改动 BLE 或核心的数据结构。

默认值不等同于硬件实现已经完成。RSENSE 阻值、NTC 曲线、SPI 引脚、MOS 高低侧和极性未确认前，系统不会发布推测出的测量值，也不会驱动均衡或功率开关。

## 分层

    BMS Core / Realtime / 后续保护、SOC、BLE
                     |
               AfeDevice 接口
                     |
              SH36735 适配器
                     |
          SH36735 SPI/CRC/寄存器驱动
                     |
            后续 Telink SPI 板级传输层

`core/` 不包含寄存器地址、字节序或 MOS 真值表。`afe/` 的公共接口使用毫伏、毫安、摄氏度十分之一度和毫瓦，避免芯片 ADC 码值穿透到业务层。芯片专属原始状态保存在 `vendor_status`，供诊断使用。

## 关键接口约定

- `BmsMeasurement` 是 AFE 单次采样结果；`BmsRealtime` 是加上序列号、极值、功率、故障与后续算法结果的发布快照。
- `cell_count`、`temperature_count` 必须随每条测量数据携带，并受最大容量约束。
- `AfeCapabilities` 由适配器声明，核心在初始化时拒绝超出 AFE 能力的产品配置。
- `AfeOps` 抽象初始化、采样、均衡、功率路径、故障读取和功耗模式；同口/分口只是 `BmsProductConfig` 的产品属性，具体 FET 真值表只能在板级层落地。
- `bms_platform_poll()` 只有在测量和故障读取均成功时更新实时状态；I/O、CRC、协议等底层失败会使平台进入 AFE 错误状态，防止继续使用陈旧数据。`BMS_STATUS_NOT_READY` 则保留就绪状态，表示尚未完成受控启用而非硬件故障。

## SH36735 当前实现状态

SH36735 驱动已实现 SPI 模式 3 使用所需的帧级事务：读命令 `0x02`、写命令 `0x01`、软件复位 `0x0B BB CC`、CRC-8/0x07/初值 0，以及 ACK 和回显校验。传输函数由板级层注入，因而该驱动不依赖 Telink 寄存器。

适配器已完成 FLAG1/FLAG2/FLAG3 的通用故障映射、ADC 原始快照和受控的模式/均衡/MOS 写入。`Sh36735RawSnapshot` 只停留在芯片层；板级回调把 ADC 码值换算为 `BmsMeasurement`。缺少单体标定、NTC/RSENSE 或检测策略时，适配器返回 `BMS_STATUS_NOT_READY` 或不置对应有效位，而不生成猜测值。

均衡、功率路径和低功耗模式还受 `Sh36735Adapter` 的显式板级许可保护，默认全为禁用。即使芯片 Capability 存在，也不得在未完成原理图、热设计和台架验证前写实际控制寄存器。详细寄存器映射与接入顺序见 `docs/sh36735.md`，更换 AFE 的影响范围见 `docs/afe_replacement_review.md`。

## 后续接入顺序

1. 确认 PCB 原理图与实测：SPI 引脚/片选、RSENSE、NTC、均衡回路、MOS 极性及驱动电平。
2. 实现 TLSR8251 SPI 板级传输层并在实板验证 SH36735 帧、CRC 与寄存器读写。
3. 完成 VADC/CADC/温度换算回调、硬件保护配置和受控均衡服务。
4. 在 `ble_sample` 最小化移植基础上接入 `BmsPlatform`、BMSLink 与独立的官方 OTA 服务。
5. 建立带 `MCU_STARTUP_8251` 的完整镜像构建与刷写验证；不得沿用旧工程误配的 8258 启动项。
