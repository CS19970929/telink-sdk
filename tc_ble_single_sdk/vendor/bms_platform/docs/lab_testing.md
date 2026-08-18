# TLSR8251 无 AFE 实验室验证

本文件定义没有 BMS 成品板、仅有官方 TLSR8251 开发板与下载器时的验证范围。实验室镜像与正式固件完全分离：正式 `build-firmware` 不启用模拟数据，也不启用 OTA Flash 写入；只有 `build-lab-*` 命令会传入实验室宏。

## 构建

在 `vendor/bms_platform/` 目录执行：

    python tools/bms.py build-lab-firmware

产物为 `build/lab_firmware/telink_bms.bin`。下载到官方 TLSR8251 开发板后，设备应广播名称 `Telink BMS`，扫描响应包含 `BMSLink`。模拟器每 500 ms 发布 20S、4 路温度和约 74 V 总压；单体电压、总压和温度在 20 秒周期内连续变化，电流则在 +400 mA 至 +1920 mA、-400 mA 至 -1920 mA 间切换。数值仅用于验证显示、单位、分片、SOC、保护、均衡请求、事件和参数回读；不是 AFE 标定值。

### GPIO_PD4

本项目的实验室镜像和生产镜像都没有引用 `GPIO_PD4`，也没有对它调用 `gpio_set_func`、`gpio_set_input_en`、`gpio_set_output_en`、`gpio_write` 或上下拉配置。因此 PD4 不承担 AFE、MOS、状态指示或 BLE 通信功能，应用层不会改变它的电平。SDK 对 TLSR825x 的定义为 Port D 的 bit4；复位后的电气状态及开发板是否接有 LED 取决于芯片和具体开发板版本，不能把它当成稳定高/低电平使用。需要指示灯或外部信号时，必须在已确认的原理图基础上单独增加板级配置。

### GPIO_PC2 主循环心跳

所有构建配置均将 `GPIO_PC2` 设为 `AS_GPIO` 推挽输出、禁用输入、初始低电平。主循环通过 SDK 的微秒计时器每 1 秒翻转一次，因此 PC2 保持 1 秒低、1 秒高的方波。该引脚是开发板运行心跳，和 BLE 连接状态、AFE 采样、MOS 控制无关；将该固件用于其他板子前必须确认 PC2 没有接到安全关键外设。

## PC 验证顺序

    cd pc_client
    python -m pip install -r requirements.txt
    python -m bms_pc.cli scan
    python -m bms_pc.cli --address <BLE地址> info
    python -m bms_pc.cli --address <BLE地址> realtime
    python -m bms_pc.cli --address <BLE地址> params
    python -m bms_pc.app

首次连接时让 Windows 完成设备发起的 Just Works 配对；参数写入和 SOC 写入只有在链路加密完成后才会被固件接受。可在 GUI 中读取、导出、比较、写入并回读参数，同时观察实时电芯、温度、功率、SOC、保护与事件页。

使用 `build/pc_client/TelinkBMS.exe` 时，点击“扫描”，双击名称为 `BMSLink`（或 `Telink BMS`）的行即可连接。无需手输地址；Windows 显示的名称取决于其是否接收到了扫描响应，因此本实验镜像通常显示为 `BMSLink`。

## OTA 实验

先用 Telink 下载器确认开发板 Flash 容量至少为 256 KiB，再执行：

    python tools/bms.py build-lab-ota-firmware
    python tools/bms.py build-lab-ota-proof-firmware

先用 Telink 下载器把基准镜像 `build/lab_ota_firmware/telink_bms.bin` 下载到开发板。该镜像版本为 `0.2.0`，它才会启用 OTA 服务；因此普通 `lab_firmware` 的 OTA 页保持禁用是预期的安全状态。

重新连接上位机，进入 OTA 页并点击“校验 OTA 信息”。必须显示“官方 Telink OTA 服务：可用”和“板级 Flash 布局已批准：是”，随后“选择镜像并升级”按钮才会启用。选择验证镜像 `build/lab_ota_proof_firmware/telink_bms.bin`，确认后开始升级：

1. 进度条达到最后一个数据块且弹出“OTA 完成”，表示 PC 收到了 SDK `0xFF06` 的成功结果，镜像传输、CRC 和 Flash 写入已通过。
2. 等待开发板自动重启并重新广播，然后点击“扫描”，双击 `BMSLink` 重新连接并点击“刷新”。
3. 仪表盘的“固件”必须从 `0.2.0` 变为 `0.2.1`。这是新镜像已经被 Bootloader 选中并实际启动的端到端证据，不只是传输完成。

如果 PC 已报告成功但重连后仍是 `0.2.0`，则不得判定为升级完成：先停止继续测试，检查是否选择了 `lab_ota_proof_firmware` 镜像、开发板 Flash 容量和 Telink 下载器的双镜像配置。升级期间不得断电或断开下载器。两个 OTA 实验镜像均固定使用 SDK 参考的 `0x20000` 次镜像槽，不可用于未知 Flash 容量、真实 BMS 板或量产板。

## 不覆盖的范围

实验室镜像不验证 SH36735 SPI/CRC、RSENSE/NTC/电压标定、AFE 硬件保护、MOS 实际动作、均衡热设计、低压写 Flash 保护和真实电池安全策略。这些项目必须待 AFE 与完整原理图、供电和负载条件具备后单独验证。
