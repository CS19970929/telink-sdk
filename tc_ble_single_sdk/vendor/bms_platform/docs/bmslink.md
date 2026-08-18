# BMSLink v1

## 定位

BMSLink 是 BMS 的业务帧协议，与 BLE GATT、UART、RS485、CAN 的报文承载方式解耦。BLE 的 RX 特征接收字节流片段，TX 特征以 Notify 发送字节流片段；分片、MTU 与重传策略属于 Transport，不能改变本文件定义的 BMSLink 帧。

## 帧格式

所有多字节整数为小端序，CRC 使用 CRC-16/CCITT-FALSE（多项式 `0x1021`、初值 `0xFFFF`、不反射、异或输出 `0x0000`）。CRC 覆盖 SOF 至 Payload 的全部字节，随后以小端序附加。

| 偏移 | 长度 | 字段 |
| --- | ---: | --- |
| 0 | 1 | SOF0，固定 `0xB5` |
| 1 | 1 | SOF1，固定 `0x4D` |
| 2 | 1 | 协议版本，当前 `1` |
| 3 | 1 | Flags：Response=`bit0`、Event=`bit1`、Error=`bit2` |
| 4 | 2 | Sequence |
| 6 | 1 | Command |
| 7 | 2 | PayloadLength，范围 0–128 |
| 9 | N | Payload |
| 9+N | 2 | CRC16 |

请求必须清除 Response/Event/Error 位；响应回显请求 Sequence 和 Command，并置 Response 位。失败响应还置 Error 位，Payload 的第一个字节为 `BmsLinkError`。异步事件置 Event 位，Sequence 为 0。

## 首批命令

| Command | 名称 | 作用 |
| ---: | --- | --- |
| `0x01` | GET_DEVICE_INFO | MCU、AFE、串数、温度数量、能力与固件版本 |
| `0x02` | GET_REALTIME | BmsRealtime 快照 |
| `0x10` / `0x11` | GET/SET_PARAMETERS | 统一参数读写 |
| `0x12` | GET_PARAMETER_SCHEMA | 参数目录、类型、范围和访问属性 |
| `0x20` | CONTROL | 受权限和安全状态约束的控制命令 |
| `0x30` / `0x31` | GET_FAULTS / GET_EVENT_LOG | 故障及事件诊断 |
| `0x40` | OTA_INFO | 返回官方 OTA 服务与镜像兼容性信息 |

协议不携带 SH36735 寄存器地址、ADC 码值或位定义。特定 AFE 诊断只能作为受控的扩展诊断载荷，不能成为普通业务命令的语义。

## 固定测试向量

空载荷的 `GET_DEVICE_INFO` 请求，版本 1、Flags 0、Sequence `0x1234`：

    B5 4D 01 00 34 12 01 00 00 CC 78

其中 CRC 为 `0x78CC`，在线路中的字节序为 `CC 78`。
