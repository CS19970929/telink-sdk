# PC 蓝牙上位机

`pc_client/` 是正式的 BMSLink PC 客户端，而不是 AFE 调试脚本。结构为：

    Tk Dashboard / CLI
              ↓
          BmsClient
              ↓
       BMSLink v1 codec
              ↓
    DemoTransport / BleakTransport

普通页面只显示设备信息、实时数据、单体、温度、参数、保护、故障、事件和 OTA 状态；没有 SH36735 寄存器解析。桌面界面支持参数读取、双击编辑、JSON 导入导出和演示设备，命令行适合自动化。

在 `pc_client/` 目录运行：

    python -m unittest discover -s tests -p "test_*.py"
    python -m bms_pc.cli --demo realtime
    python -m bms_pc.app

首次连接真实 Windows BLE 设备前安装可选依赖：

    python -m pip install -r requirements.txt
    python -m bms_pc.cli scan
    python -m bms_pc.cli --address <BLE地址> realtime

真实设备的参数写入仍由固件安全授权状态决定；客户端不会绕过该门禁。OTA 页面通过 `OTA_INFO` 显示官方服务和板级布局状态。Telink OTA 数据服务已在固件中注册，但由于当前 BMS 板的 Flash 分区、固件签名策略和断电恢复尚未确认，PC 端刻意不提供“开始刷写”按钮，避免对未知产品硬件产生不可恢复操作。
