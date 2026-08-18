# 移动端 BMS 客户端

`mobile_app/` 是 Android/iOS 共用的 Flutter 客户端。它使用 BMSLink v1 和同一份 `bms_schema.yaml`，不包含 SH36735 寄存器、ADC 码或 MOS 真值表。

## 功能

- 扫描 BMS 自定义服务、连接、订阅 TX Notify，并把 20 字节写片段与任意长度 Notify 片段还原为 BMSLink 帧；
- 读取设备信息、实时数据、电芯、温度、故障、OTA 服务信息；
- 分页读取 Parameter/Schema，并用固件返回的读写范围校验显示；输入参数后发送原子 `SET_PARAMETERS`；
- 以 Bundle 形式携带公开 Schema，用于显示稳定 Key 与单位；仓库测试会校验它和 `schema/bms_schema.yaml` 完全一致；
- Android/iOS 权限片段位于 `mobile_app/platform_setup/`。

BLE UUID 与固件一致：Service `B1A50001-A00D-4692-9144-5E8E20689457`、RX `...9458`、TX `...9459`。移动端不依赖 AFE 名称；设备信息中的 AFE 枚举仅用于诊断展示。

## 初始化与构建

当前 SDK 工作站没有安装 Flutter/Dart，因此未在本机执行 `flutter analyze` 或真机构建。先按 Flutter 官方 Windows 安装说明安装 Flutter，再在 `mobile_app/` 执行：

    flutter create --platforms=android,ios .
    flutter pub get
    flutter analyze
    flutter test
    flutter run

`flutter create` 只生成 Android/iOS Runner；不会覆盖现有 `lib/` 或 `pubspec.yaml`。随后将 `platform_setup/AndroidManifest.xml.snippet` 中的权限合并到 Android Manifest，并将 `Info.plist.snippet` 合并到 iOS Runner。

`flutter_reactive_ble` 负责扫描、连接、特征写入、Notify 和 MTU 协商能力；客户端保守使用 20 字节写分片，因此不要求 iOS 协商 MTU。设备端 BMSLink 解码器支持流式接收，固件 Notify 也可被移动端重新组帧。

## 写入与 OTA 安全边界

写参数仍由设备端已授权会话决定。固件只在 SDK SMP 配对加密完成事件后授权当前会话；未配对、配对失败或连接断开时均拒绝写入。App 不绕过该门禁，并会显示设备返回的拒绝原因。

OTA 状态页只读取官方 OTA Service 能力。Telink OTA 数据传输不走 BMSLink；PC 端已经实现受固件板级门禁、二次确认和结果通知保护的官方 OTA 传输。移动端暂不提供刷写入口，直到 Flash 分区、镜像兼容性、掉电恢复以及移动端真机 BLE 回归都完成；届时必须复用 PC 的 16 字节块、CRC16、结果通知和镜像大小字段校验规则，并额外加入版本/签名检查与中断恢复界面。
