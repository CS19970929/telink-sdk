# PC 蓝牙上位机

`pc_client/` 是正式的 BMSLink PC 客户端，而不是 AFE 调试脚本。结构为：

    Tk Dashboard / CLI
              ↓
          BmsClient
              ↓
       BMSLink v1 codec
              ↓
    DemoTransport / BleakTransport

普通页面只显示设备信息、实时数据、单体、温度、参数、保护、故障、事件和 OTA 状态；没有 SH36735 寄存器解析。桌面界面支持读取全部参数、双击编辑、JSON 导入导出、与已导出文件比较差异和演示设备，命令行适合自动化。

在 `pc_client/` 目录运行：

    python -m unittest discover -s tests -p "test_*.py"
    python -m bms_pc.cli --demo realtime
    python -m bms_pc.app

首次连接真实 Windows BLE 设备前安装可选依赖：

    python -m pip install -r requirements.txt
    python -m bms_pc.cli scan
    python -m bms_pc.cli --address <BLE地址> realtime
    python -m bms_pc.cli --address <BLE地址> ota-info
    python -m bms_pc.cli --address <BLE地址> --image <telink_bms.bin> --confirm-ota ota

真实设备的参数写入仍由固件安全授权状态决定；客户端不会绕过该门禁。

## Telink OTA

桌面端与 CLI 都实现了 SDK 官方 legacy OTA：先发送 `0xFF01` 启动命令，再以 `块序号:u16 + 16 字节数据 + CRC16:u16` 连续发送 20 字节写入，最后发送带反码的 `0xFF02` 结束命令，并且必须等到服务端 `0xFF06` 成功结果通知才报告完成。最后一个数据块用 `0xFF` 补足。客户端只接受 `.bin`，读取镜像偏移 `0x18` 的小端固件大小，并拒绝异常大小、未声明大小和超过 16 位块序号上限的文件。

这不是默认开放的刷写入口。固件默认 `BMS_OTA_LAYOUT_APPROVED=0`：SDK OTA server 不初始化，GATT 回调不调用 Flash 写入器，`OTA_INFO` 因此报告不可用。完成目标板的 Flash 容量/双镜像地址、最大镜像大小、量产保护、稳定供电和断电恢复验证后，必须把批准宏、`BMS_OTA_FIRMWARE_SIZE_KB` 和 `BMS_OTA_BOOT_ADDRESS` 一并写成经审核的配置，再构建并进行受控实机回归。即使设备报告已批准，GUI 仍需二次确认，CLI 也必须显式给出 `--confirm-ota`。
