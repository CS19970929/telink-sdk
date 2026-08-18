# 参数、保护与运行策略

## 参数模型

`BmsParameters` 只描述 BMS 业务策略。ID 在 `include/bms/bms_parameters.h` 固定，范围、默认值和读写属性在 `core/bms_parameters.c` 的描述表统一维护；它们不是 SH36735XX 寄存器地址。当前覆盖单体过压/欠压、充放电过流、充放电温度、单体压差告警、容量/SOC、均衡和加热阈值。

一次 `SET_PARAMETERS` 可以包含 1–21 个条目，条目为 `ParameterId:u16 + Value:i32`。固件先在副本中应用所有写入，再同时校验边界和交叉约束，全部通过才替换当前参数，因而不会出现只写入半组保护阈值的状态。`GET_PARAMETERS` 每条返回 `Id:u16 + Type:u8 + Value:i32`，单帧最多 18 条；`GET_PARAMETER_SCHEMA` 每条返回 `Id:u16 + Type:u8 + Flags:u8 + Min:i32 + Max:i32 + Default:i32`，单帧最多 7 条。两者均可用 `StartId:u16 + Count:u8` 分页。

写参数和设置 SOC 都要求固件注册了“已授权写入”回调。BLE 固件外壳按 SDK 外设 SMP 顺序启动 Bondable 的未认证配对加密；只有收到 `GAP_EVT_SMP_CONN_ENCRYPTION_DONE` 后，本连接才会通过此回调。连接建立、断开或配对失败均会清除授权。它避免把裸 GATT 写入误视为授权；量产前仍须审核 SMP Flash 扇区、配对 UX 以及是否需要 MITM/安全连接等级。

`schema/bms_schema.yaml` 是供文档、PC/App 导入导出使用的版本化公开清单；MCU 不解析 YAML，仍使用静态描述表，避免引入动态解析器和堆内存。

## 保护与控制边界

`BmsProtectionMonitor` 处理通用单体 OV/UV、充/放电 OC、充/放电温度和 AFE 故障。报警在条件出现时立即反映；保护动作需满足 `protect.delay.ms`，并按独立释放条件恢复。当前电流约定为：**正值表示放电，负值表示充电**。

保护输出首先是逻辑功率命令。对于默认同口拓扑，只要任一方向必须禁止，就同时禁止充、放，避免把分口假设带到同口产品。`BmsPlatform` 仅在 AFE `set_power`/`set_balance` 成功时把结果写入 `BmsRealtime.power_state` 和 `balance_cells_mask`；不支持或未就绪时仅保留请求，不宣称已驱动 MOS 或均衡。

SOC 是“库仑积分 + 静置时小步 OCV 修正”的基础实现，SOH 初始为 1000‰。它提供稳定接口，不应被当作已完成量产标定算法。均衡仅在单体、温度和电流数据有效、无保护、最高单体达到起始电压且压差满足条件时请求；加热仅在有充电器、温度有效、无保护且低温条件满足时请求。实际加热输出仍由未来板级能力实现。

## 配置与日志

`BmsConfigStore` 定义不依赖芯片的双槽持久化协议：每槽 80 字节，包含魔数、版本、世代号、固定序列化载荷和 CRC32。保存顺序为“擦除非当前槽 → 写入 → 读回 CRC 验证”，加载时选取最新有效世代。板级层必须提供两个非重叠 Flash 槽的读/擦/写回调；在 Flash 容量、OTA 布局和擦除扇区确认前，固件不会调用该接口。

`BmsEventLog` 是 32 条 RAM 环形诊断日志，记录保护、AFE 故障、参数和 SOC 的变更，使用 `GET_EVENT_LOG` 分页读取。持久化故障历史的磨损预算和 Flash 区域要在板级布局确认后另行接入。
