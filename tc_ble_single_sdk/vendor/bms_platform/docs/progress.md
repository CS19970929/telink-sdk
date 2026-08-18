# 实现进度

本文件以 `todo.md` 的目标为准记录实际完成度；“代码完成”不等于“已在目标板验证”。

| 范围 | 状态 | 证据 / 说明 |
| --- | --- | --- |
| SDK、工具链、Git | 已完成 | SDK 基线提交 `873ea98`；固定 TC32 与 Cppcheck 门禁。 |
| 通用数据模型与 AFE 边界 | 已完成 | `core/`、`include/bms/afe/`、SH36735 适配器。 |
| SH36735 寄存器 SPI/CRC 边界 | 已完成（驱动级） | SPI/CRC、串数、原始采样、均衡、功率、模式、状态/故障及受控清除均已实现；板级 SPI、标定与实际控制许可仍待原理图和台架验证。 |
| BMSLink 协议内核 | 已完成 | v1 编解码器、CRC 和固定测试向量。 |
| 完整 TLSR8251 BLE 固件 | 已完成（编译级） | 自有 BLE 外壳、Custom GATT、BMSLink 接入、官方 OTA 服务与 `MCU_STARTUP_8251` 完整镜像均已链接并通过 SDK 镜像检查；尚未实机验证。 |
| Parameter / Schema | 已完成（编译级） | 21 个稳定业务 ID、分页 Schema/读写协议、交叉校验、YAML 公共清单与授权写入门禁。 |
| Protection / SOC / Balance / Heating | 已完成（编译级） | 通用保护、基础 SOC、均衡/加热请求与同口安全规则已接入业务层；实际标定与驱动待硬件。 |
| 配置存储、事件、诊断 | 已完成（编译级；512 KiB 实板回归待执行） | 双槽 CRC 存储、RAM 事件环形日志和 BMSLink 查询已完成；`0x72000/0x73000` 已隔离给实验配置。参数/名称保存改为主循环延迟执行，并在写后读回验证；最终“写入→复位→回读”的 512 KiB 开发板回归待设备恢复广播后执行。真实 BMS 板的 Flash 布局仍待板级审核。 |
| 官方 OTA 集成 | 已完成（编译级，默认安全禁用） | 官方服务、legacy 传输格式、180 秒超时和结果回调已接入；`BMS_OTA_LAYOUT_APPROVED=0` 时 SDK Flash 写入器不会初始化或被调用。批准 OTA 时还强制提供经审核的镜像大小与启动槽位，并在 SDK 要求的 `cpu_wakeup_init()` 前配置。分区/断电恢复仍待实机验证。 |
| PC 上位机 | 已完成（演示/通信级） | Tk Dashboard、Cells、Temperature、参数导入导出、故障/事件、CLI、DemoTransport 与可选 Bleak 实机传输均已实现并有离线测试；包含受板级批准门禁的 Telink OTA 数据传输和成功结果确认。真实 BLE/授权写入/OTA 仍需硬件验证。 |
| 移动 App | 已完成（源码级） | Flutter Android/iOS 单代码库实现了扫描、连接、分片 BMSLink、实时/电芯/温度/参数/故障/OTA 信息页，并携带受测试约束的公共 Schema；本机未安装 Flutter/Dart，尚未执行移动构建或真机 BLE 验证。 |
| 无 AFE 实验室验证固件 | 已完成（编译级） | `build-lab-firmware` 提供 20S/4 温度确定性模拟数据，且不调用 AFE/SPI/MOS；`build-lab-ota-firmware` 在官方 512 KiB 开发板上额外验证 SDK 参考 OTA 次镜像槽；`0.2.2` 控制镜像支持名称、参数和配置 Flash 验证。 |

## 硬件待确认项

在得到原理图和实机前，以下能力只能以安全禁用状态存在：SH36735 SPI 引脚与片选、RSENSE 电流标定、NTC 曲线、单体/总压标定、均衡热策略、同口 MOS 高低侧拓扑、预充与休眠唤醒引脚。任何代码不会把这些未知项替换为猜测值。SH36735 驱动细节见 `docs/sh36735.md`，AFE 替换审查见 `docs/afe_replacement_review.md`。
