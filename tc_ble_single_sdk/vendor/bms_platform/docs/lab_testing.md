# TLSR8251 无 AFE 实验室验证

本文件定义没有 BMS 成品板、仅有官方 TLSR8251 开发板与下载器时的验证范围。实验室镜像与正式固件完全分离：正式 `build-firmware` 不启用模拟数据，也不启用 OTA Flash 写入；只有 `build-lab-*` 命令会传入实验室宏。

## 构建

在 `vendor/bms_platform/` 目录执行：

    python tools/bms.py build-lab-firmware

产物为 `build/lab_firmware/telink_bms.bin`。下载到官方 TLSR8251 开发板后，设备应广播名称 `Telink BMS`，扫描响应包含 `BMSLink`。模拟器每 500 ms 发布 20S、4 路温度、约 74 V 总压和在 +1200 mA / -800 mA 间切换的电流。数值仅用于验证显示、单位、分片、SOC、保护、均衡请求、事件和参数回读；不是 AFE 标定值。

## PC 验证顺序

    cd pc_client
    python -m pip install -r requirements.txt
    python -m bms_pc.cli scan
    python -m bms_pc.cli --address <BLE地址> info
    python -m bms_pc.cli --address <BLE地址> realtime
    python -m bms_pc.cli --address <BLE地址> params
    python -m bms_pc.app

首次连接时让 Windows 完成设备发起的 Just Works 配对；参数写入和 SOC 写入只有在链路加密完成后才会被固件接受。可在 GUI 中读取、导出、比较、写入并回读参数，同时观察实时电芯、温度、功率、SOC、保护与事件页。

## OTA 实验

先用 Telink 下载器确认开发板 Flash 容量至少为 256 KiB，再执行：

    python tools/bms.py build-lab-ota-firmware

把该镜像下载到开发板，再用 GUI 的 OTA 页或以下命令选择同一实验室镜像：

    cd pc_client
    python -m bms_pc.cli --address <BLE地址> --image ..\build\lab_ota_firmware\telink_bms.bin --confirm-ota ota

OTA 完成标准是 PC 收到 SDK `0xFF06` 成功结果，随后开发板重启并再次广播。升级期间不得断电或断开下载器。该镜像固定使用 SDK 参考的 `0x20000` 次镜像槽，不可用于未知 Flash 容量、真实 BMS 板或量产板。

## 不覆盖的范围

实验室镜像不验证 SH36735 SPI/CRC、RSENSE/NTC/电压标定、AFE 硬件保护、MOS 实际动作、均衡热设计、低压写 Flash 保护和真实电池安全策略。这些项目必须待 AFE 与完整原理图、供电和负载条件具备后单独验证。
