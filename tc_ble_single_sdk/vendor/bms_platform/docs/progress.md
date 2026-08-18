# 实现进度

本文件以 `todo.md` 的目标为准记录实际完成度；“代码完成”不等于“已在目标板验证”。

| 范围 | 状态 | 证据 / 说明 |
| --- | --- | --- |
| SDK、工具链、Git | 已完成 | SDK 基线提交 `873ea98`；固定 TC32 与 Cppcheck 门禁。 |
| 通用数据模型与 AFE 边界 | 已完成 | `core/`、`include/bms/afe/`、SH36735 适配器。 |
| SH36735 寄存器 SPI/CRC 边界 | 部分完成 | 已有帧级读写和故障映射；尚缺板级 SPI、标定与控制使能。 |
| BMSLink 协议内核 | 已完成 | v1 编解码器、CRC 和固定测试向量。 |
| 完整 TLSR8251 BLE 固件 | 已完成（编译级） | 自有 BLE 外壳、Custom GATT、BMSLink 接入、官方 OTA 服务与 `MCU_STARTUP_8251` 完整镜像均已链接并通过 SDK 镜像检查；尚未实机验证。 |
| Parameter / Schema | 未开始 | 将在 BMSLink 命令处理前实现。 |
| Protection / SOC / Balance / Heating | 未开始 | 需要先有参数与运行时调度。 |
| 配置存储、事件、诊断 | 未开始 | 将独立定义 Flash 布局和原子提交。 |
| 官方 OTA 集成 | 已完成（编译级） | 已注册官方 OTA 服务和回调；Flash 分区与断电恢复尚待实机验证。 |
| PC 上位机 | 未开始 | 将使用 BMSLink Python 客户端和 BLE Transport。 |
| 移动 App | 未开始 | 协议与参数稳定后实施。 |

## 硬件待确认项

在得到原理图和实机前，以下能力只能以安全禁用状态存在：SH36735 SPI 引脚与片选、RSENSE 电流标定、NTC 曲线、单体/总压标定、均衡热策略、同口 MOS 高低侧拓扑、预充与休眠唤醒引脚。任何代码不会把这些未知项替换为猜测值。
