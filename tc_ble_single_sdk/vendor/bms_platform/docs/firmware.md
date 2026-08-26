# TLSR8251 固件与 BLE Transport

## 固件边界

`firmware/` 是 BMS 平台自己的 BLE 外壳，而不是拷贝或修改官方 `vendor/ble_sample`。它仅复用 SDK 公开的初始化、BLE 栈、GATT 和 OTA API，并把 BMS 业务留在 `core/`、`protocol/` 与 AFE 层。

完整镜像构建固定编译 `boot/B85/cstartup_825x.S` 与 SDK 提供的 `common/div_mod.S`，显式定义 `MCU_STARTUP_8251=1`，并链接当前 SDK 的 `proj_lib/liblt_825x.a` 和 `project/tlsr_tc32/B85/boot.link`。构建脚本还编译 SDK 的 `common/`、`drivers/B85/`、`vendor/common/`、`application/print/` 运行时源文件；其中 `div_mod.S` 提供 TC32 所需的除法和 BLE CRC 例程。这避免官方 Eclipse 示例中 8258 启动定义与本项目 TLSR8251 目标不一致的问题。

## GATT

自定义 BMS 服务使用以下 UUID（128-bit 字节序以 SDK 属性表为准）：

| 项目 | UUID |
| --- | --- |
| BMS Service | `B1A50001-A00D-4692-9144-5E8E20689457` |
| BMS RX | `B1A50001-A00D-4692-9144-5E8E20689458` |
| BMS TX | `B1A50001-A00D-4692-9144-5E8E20689459` |

RX 支持 Write 和 Write Without Response；TX 为 Notify。它们传输连续 BMSLink 字节流，接收端可跨 GATT 写入拼接，发送端在 MTU 64 的配置下分片为最多 61 字节的 Notify。应用层一次只保留一个待发送 BMSLink 响应，客户端在收到完整帧前不应并发发送第二个请求；繁忙时应按协议重试。

## OTA

OTA 使用 SDK 官方 Telink OTA 服务及 `otaWrite` 入口，与 BMS Service 并列，不能通过 BMSLink 自行重写固件数据传输。服务端采用 SDK legacy OTA 格式：启动 `0xFF01`，每个 20 字节 PDU 为 `块序号:u16 + 数据:16 + CRC16:u16`，结束为 `0xFF02 + 最后块序号 + 反码`，通过 `0xFF06` 通知结果。PC 客户端按此流程实现，并等待成功结果才显示完成。

所有固件默认使用已配置的 OTA 布局：124 KiB 镜像、`0x20000` 次镜像槽。构建时 SDK 会在 `cpu_wakeup_init()` 前接收镜像大小和启动地址，并初始化 OTA server；配置使用 `BMS_OTA_PROCESS_TIMEOUT_SECONDS=180` 及 20 ms 数据包间隔。目标板仍必须先确认 Flash 容量、双镜像分区、写保护、稳定供电和断电恢复流程，`build-firmware` 通过检查器不等于已完成实机验证。

## BLE 安全会话

默认启用 SDK 的可绑定、无输入输出配对及链路加密。连接、断开和配对失败都会清除 BMSLink 写入授权；只有收到 SDK 的“连接加密完成”主机事件后，参数写入与 SOC 控制才被放行。OTA 是独立的 SDK 通道，除上述 Flash 布局总开关外，量产方案还应把配对策略、调试口及升级授权纳入板级安全评审。

## 板级安全边界

本固件的 `app_config.h` 仅为了验证 SDK 编译/链接提供通用 825x EVK 配置；它没有声明任何 BMS 实际引脚。SH36735 SPI、MOS、均衡、NTC 和唤醒引脚均没有被初始化或驱动。拿到原理图后必须在 `board/` 定义正式引脚配置，并完成实机验证后才允许刷写。
