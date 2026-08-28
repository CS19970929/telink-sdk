# Windows BMS 上位机构建与审核记录

## 环境

- Windows 10 22H2
- .NET SDK 7.0.400
- MSBuild 17.7.1
- 目标框架：`net7.0`、`net7.0-windows10.0.19041.0`
- C# 语言版本：11.0

## 已验证命令

在 `host_tools/windows/` 下执行：

```powershell
dotnet clean .\BmsHost.sln
dotnet restore .\BmsHost.sln
dotnet build .\BmsHost.sln -c Release
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-win.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\publish-win-x64.ps1
```

上述构建与脚本均已在当前开发环境中实际通过，Release 构建为 0 警告、0 错误。默认发布为 win-x64 self-contained single-file，输出文件为：

`publish\win-x64\BmsHost.Win.exe`

## 本次修复

- 为 Windows transport、OTA 服务和 WPF 主窗口补齐 `System.IO` 引用，解决 .NET 7 编译错误。
- 修正 OTA 文件路径的可空流分析警告。
- 连接初始化提前纳入统一 transport 生命周期，连接失败时也会释放新建对象。
- Serial/BLE 主动断开或异常断开时立即结束 Modbus/OTA 等待任务，避免等待到超时。
- 保留请求 semaphore 串行化，并收紧请求与断开之间的竞态窗口。

## 审核边界

已静态检查 Modbus CRC、响应长度、BLE NUS 分包重组、自动重试、轮询/手工操作/OTA 的 UI 操作锁、取消和 transport 资源释放。未连接真实 BMS，因此串口唤醒首帧重试、BLE 通知时序、参数写入、MOS 控制及 OTA 成功/失败结果仍需接实机验证。

本次未修改 MCU 固件目录 `tc_ble_single_sdk/vendor/ble_sample/`。
