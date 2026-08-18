# AFE 可替换审查

审查对象是默认 SH3673520/20S/4 温度/同口产品。目标不是更换 AFE 时零修改，而是把影响收敛在 AFE 和产品板级范围。

| 模块 | 更换为 AFE_B 时是否应修改 | 结论 |
| --- | --- | --- |
| `afe/afe_interface.*` | 否（除非 AFE_B 需要新的通用业务能力） | 只使用采样、功率、均衡、故障和模式语义。 |
| `afe/sh36735/` | 是 | 替换为 `afe/afe_b/` 的 Driver/Adapter。 |
| `board/` | 是 | SPI/I2C、换算、MOS 拓扑与控制许可属于产品板。 |
| `core/` 的保护、SOC、均衡、加热 | 否 | 只读取 `BmsRealtime` 和 `AfeFaultSnapshot`。 |
| BMSLink、BLE/GATT | 否 | 只传统一物理量、能力和故障，不含寄存器/ADC 码。 |
| 参数 Schema、配置存储 | 否 | 参数描述 BMS 策略，未使用 SH36735 地址或位定义。 |
| PC 客户端、后续移动端 | 否 | 客户端走 BMSLink，不解析 AFE 原始帧。 |
| 硬件能力声明 | 允许少量修改 | 通过 `AfeCapabilities` 和产品配置反映实际能力。 |

本次审查新增 `tests/test_afe_boundary.py`：它检查核心、协议、固件及 PC 业务代码不包含 SH36735 名称，并检查 20S 均衡掩码的寄存器映射。该测试是静态边界证据；TC32 编译和实板验证仍是独立门禁。

结论：现有影响范围符合“AFE Driver、AFE Adapter、Board/Product Configuration、Capabilities”的预期。若未来需要把 AFE_B 的特殊 ADC 模式或寄存器诊断暴露给客户端，应新增受控的诊断扩展，不能污染普通实时数据、参数或控制命令。
