"""Tk desktop UI for BMSLink; run ``python -m bms_pc.app`` from pc_client."""

from __future__ import annotations

import asyncio
import json
from pathlib import Path
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

from .client import BmsClient, BmsProtocolError
from .parameter_catalog import parameter_label
from .transport import BleakTransport, DemoTransport


AUTO_REFRESH_INTERVAL_MS = 1000


class AsyncRunner:
    def __init__(self) -> None:
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def call(self, coroutine):
        return asyncio.run_coroutine_threadsafe(coroutine, self.loop).result()

    def submit(self, coroutine):
        """在线程事件循环中提交协程，但不阻塞 Tk 界面线程。"""
        return asyncio.run_coroutine_threadsafe(coroutine, self.loop)

    def close(self) -> None:
        self.loop.call_soon_threadsafe(self.loop.stop)
        self.thread.join(timeout=1)


class BmsDesktop:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Telink BMS 上位机")
        self.root.minsize(920, 600)
        self.runner = AsyncRunner()
        self.client: BmsClient | None = None
        self._parameters: dict[int, int] = {}
        self._parameter_schema = {}
        self._device_info = None
        self._ble_name: str | None = None
        self._connection_text = "未连接"
        self._refresh_in_progress = False
        self._refresh_after_id: str | None = None
        self.auto_refresh = tk.BooleanVar(value=True)
        self._build()
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _build(self) -> None:
        toolbar = ttk.Frame(self.root, padding=8)
        toolbar.pack(fill=tk.X)
        ttk.Label(toolbar, text="BLE 地址").pack(side=tk.LEFT)
        self.address = tk.StringVar()
        ttk.Entry(toolbar, textvariable=self.address, width=28).pack(side=tk.LEFT, padx=6)
        ttk.Button(toolbar, text="扫描", command=self.scan).pack(side=tk.LEFT)
        ttk.Button(toolbar, text="连接", command=self.connect).pack(side=tk.LEFT, padx=4)
        ttk.Button(toolbar, text="演示设备", command=self.connect_demo).pack(side=tk.LEFT)
        ttk.Button(toolbar, text="刷新", command=self.refresh).pack(side=tk.LEFT, padx=4)
        ttk.Button(toolbar, text="蓝牙名", command=self.edit_ble_name).pack(side=tk.LEFT)
        ttk.Checkbutton(toolbar, text="自动刷新（1 秒）", variable=self.auto_refresh,
                        command=self._on_auto_refresh_changed).pack(side=tk.LEFT, padx=8)
        self.status = tk.StringVar(value="未连接")
        ttk.Label(toolbar, textvariable=self.status).pack(side=tk.RIGHT)

        self.tabs = ttk.Notebook(self.root)
        self.tabs.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0, 8))
        self.dashboard = tk.Text(self.tabs, height=20, state=tk.DISABLED)
        self.tabs.add(self.dashboard, text="仪表盘")
        self.cells = self._table("单体电压", ("序号", "mV"))
        self.temps = self._table("温度", ("序号", "0.1°C", "°C"))
        self.parameters = self._table("参数（双击编辑；仅授权设备允许写入）",
                                      ("ID", "名称", "单位", "当前值", "范围", "访问"))
        self.parameters.bind("<Double-1>", self.edit_parameter)
        self.faults = self._table("保护 / 故障", ("类别", "位图（十六进制）"))
        self.events = self._table("事件日志", ("时间 ms", "类型", "严重度", "变更前", "变更后"))
        self.ota_text = tk.Text(self.tabs, height=12, state=tk.DISABLED)
        ota_frame = ttk.Frame(self.tabs)
        self.ota_text.pack(in_=ota_frame, fill=tk.BOTH, expand=True, padx=8, pady=8)
        ota_actions = ttk.Frame(ota_frame)
        ota_actions.pack(pady=(0, 8))
        ttk.Button(ota_actions, text="校验 OTA 信息", command=self.show_ota).pack(side=tk.LEFT)
        self.ota_update_button = ttk.Button(ota_actions, text="选择镜像并升级",
                                            command=self.start_ota, state=tk.DISABLED)
        self.ota_update_button.pack(side=tk.LEFT, padx=6)
        self.ota_progress = ttk.Progressbar(ota_frame, mode="determinate", maximum=1)
        self.ota_progress.pack(fill=tk.X, padx=8, pady=(0, 8))
        self.tabs.add(ota_frame, text="OTA")

        actions = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        actions.pack(fill=tk.X)
        ttk.Button(actions, text="导出参数", command=self.export_parameters).pack(side=tk.LEFT)
        ttk.Button(actions, text="导入并写入参数", command=self.import_parameters).pack(side=tk.LEFT, padx=6)
        ttk.Button(actions, text="与文件比较参数", command=self.diff_parameters).pack(side=tk.LEFT)
        ttk.Label(actions, text="OTA 仅在设备显式批准 Flash 布局后开放。").pack(side=tk.RIGHT)

    def _table(self, title: str, columns: tuple[str, ...]) -> ttk.Treeview:
        frame = ttk.Frame(self.tabs)
        table = ttk.Treeview(frame, columns=columns, show="headings")
        for column in columns:
            table.heading(column, text=column)
            table.column(column, width=140, anchor=tk.CENTER)
        table.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)
        self.tabs.add(frame, text=title)
        return table

    def _call(self, coroutine):
        if self.client is None:
            raise RuntimeError("请先连接设备")
        return self.runner.call(coroutine)

    def connect_demo(self) -> None:
        self._connect(BmsClient(DemoTransport()), None, "已连接演示设备")

    def connect(self) -> None:
        address = self.address.get().strip()
        if not address:
            messagebox.showwarning("缺少地址", "请先扫描或输入 BLE 地址。")
            return
        self._connect(BmsClient(BleakTransport()), address, f"已连接 {address}")

    def _connect(self, client: BmsClient, address: str | None, text: str) -> None:
        try:
            self.runner.call(client.connect(address))
            self.client = client
            self._connection_text = text
            self._parameters = {}
            self._parameter_schema = {}
            self._device_info = None
            self._ble_name = None
            self.status.set(text)
            self.refresh()
            self._schedule_auto_refresh()
        except Exception as error:
            messagebox.showerror("连接失败", str(error))

    def scan(self) -> None:
        try:
            devices = self.runner.call(BleakTransport.scan())
            if not devices:
                messagebox.showinfo("扫描", "未发现 BLE 设备。")
                return
            self._show_scan_results(devices)
        except Exception as error:
            messagebox.showerror("扫描失败", str(error))

    def _show_scan_results(self, devices: list[tuple[str, str]]) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title("扫描结果（双击设备直接连接）")
        dialog.transient(self.root)
        dialog.minsize(480, 320)
        dialog.geometry("560x360")
        dialog.grab_set()

        ttk.Label(dialog, text="双击目标设备即可连接；名称包含 BMS 的设备会优先显示。", padding=8).pack(anchor=tk.W)
        frame = ttk.Frame(dialog, padding=(8, 0, 8, 8))
        frame.pack(fill=tk.BOTH, expand=True)
        table = ttk.Treeview(frame, columns=("address", "name"), show="headings")
        table.heading("address", text="BLE 地址")
        table.heading("name", text="设备名称")
        table.column("address", width=180, anchor=tk.CENTER, stretch=False)
        table.column("name", width=330, anchor=tk.W)
        scrollbar = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=table.yview)
        table.configure(yscrollcommand=scrollbar.set)
        table.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        preferred_item = None
        for address, name in sorted(devices, key=lambda item: ("bms" not in item[1].lower(), item[1], item[0])):
            item = table.insert("", tk.END, values=(address, name))
            if preferred_item is None and "bms" in name.lower():
                preferred_item = item
        if preferred_item is not None:
            table.selection_set(preferred_item)
            table.focus(preferred_item)

        def connect_selected(_: tk.Event | None = None) -> None:
            selected = table.selection()
            if not selected:
                messagebox.showwarning("未选择设备", "请选择要连接的 BLE 设备。", parent=dialog)
                return
            address = str(table.item(selected[0], "values")[0])
            dialog.destroy()
            self.address.set(address)
            self.connect()

        buttons = ttk.Frame(dialog, padding=(8, 0, 8, 8))
        buttons.pack(fill=tk.X)
        ttk.Button(buttons, text="连接选中设备", command=connect_selected).pack(side=tk.LEFT)
        ttk.Button(buttons, text="取消", command=dialog.destroy).pack(side=tk.LEFT, padx=6)
        table.bind("<Double-1>", connect_selected)
        table.focus_set()

    @staticmethod
    def _replace(table: ttk.Treeview, rows: list[tuple]) -> None:
        for item in table.get_children():
            table.delete(item)
        for row in rows:
            table.insert("", tk.END, values=row)

    async def _fetch_refresh_data(self, client: BmsClient, full: bool) -> dict:
        data = {"realtime": await client.realtime(), "faults": await client.faults()}
        if not full:
            return data
        data["info"] = await client.device_info()
        data["parameters"] = await client.all_parameters()
        data["schema"] = await client.all_schema()
        data["events"] = await client.events()
        try:
            data["ble_name"] = await client.ble_name()
        except BmsProtocolError:
            # 0.2.1 及更早固件尚未定义名称查询命令，仍可正常查看实时数据。
            data["ble_name"] = None
        return data

    def _render_realtime(self, realtime, faults: tuple[int, int, int]) -> None:
        info = self._device_info
        name = self._ble_name if self._ble_name is not None else "固件未提供"
        identity = "设备信息读取中"
        if info is not None:
            identity = (f"固件: {info.firmware[0]}.{info.firmware[1]}.{info.firmware[2]}\n"
                        f"MCU: TLSR825{info.mcu - 0x50}    AFE 类型: {info.afe_kind}\n"
                        f"串数 / 温度: {info.cell_count}S / {info.temperature_count}")
        summary = (
            f"{identity}\n蓝牙名: {name}\n\n"
            f"采样时间: {realtime.timestamp_ms} ms\n"
            f"总压: {realtime.pack_voltage_mv} mV\n电流: {realtime.current_ma} mA\n"
            f"功率: {realtime.power_mw} mW\nSOC: {realtime.soc_permil / 10:.1f}%\n"
            f"SOH: {realtime.soh_permil / 10:.1f}%\n压差: {realtime.cell_delta_mv} mV\n"
            f"均衡掩码: 0x{realtime.balance_mask:08X}"
        )
        self.dashboard.configure(state=tk.NORMAL); self.dashboard.delete("1.0", tk.END)
        self.dashboard.insert(tk.END, summary); self.dashboard.configure(state=tk.DISABLED)
        self._replace(self.cells, [(index + 1, value) for index, value in enumerate(realtime.cells_mv)])
        self._replace(self.temps, [(index + 1, value, f"{value / 10:.1f}")
                                   for index, value in enumerate(realtime.temperatures_decic)])
        alarms, protections, fault_flags = faults
        self._replace(self.faults, [("报警", f"0x{alarms:08X}"), ("保护", f"0x{protections:08X}"),
                                    ("故障", f"0x{fault_flags:08X}")])

    def _finish_refresh(self, future, client: BmsClient, full: bool, silent: bool) -> None:
        try:
            data = future.result()
            error = None
        except Exception as exception:
            data = None
            error = exception

        def apply() -> None:
            self._refresh_in_progress = False
            if client is not self.client:
                return
            if error is not None:
                if silent:
                    self.status.set(f"自动刷新失败：{error}")
                else:
                    messagebox.showerror("刷新失败", str(error))
                return
            if full:
                self._device_info = data["info"]
                self._ble_name = data["ble_name"]
                self._parameters = {item.parameter_id: item.value for item in data["parameters"]}
                self._parameter_schema = {item.parameter_id: item for item in data["schema"]}
                parameter_rows = []
                for item in data["parameters"]:
                    schema = self._parameter_schema.get(item.parameter_id)
                    label, unit = parameter_label(item.parameter_id)
                    limits = "未提供" if schema is None else f"{schema.minimum} .. {schema.maximum}"
                    if schema is None:
                        access = "读"
                    elif schema.flags & 0x02:
                        access = "读/写/持久" if schema.flags & 0x04 else "读/写"
                    else:
                        access = "只读"
                    parameter_rows.append((f"0x{item.parameter_id:04X}", label, unit, item.value, limits, access))
                self._replace(self.parameters, parameter_rows)
                self._replace(self.events, [(item.timestamp_ms, item.type, item.severity, item.before, item.after)
                                            for item in data["events"]])
            self._render_realtime(data["realtime"], data["faults"])
            self.status.set(f"{self._connection_text} | 实时数据已更新")

        try:
            self.root.after(0, apply)
        except (RuntimeError, tk.TclError):
            pass

    def refresh(self, full: bool = True, silent: bool = False) -> None:
        if self.client is None:
            if not silent:
                messagebox.showwarning("未连接", "请先连接设备。")
            return
        if self._refresh_in_progress:
            return
        self._refresh_in_progress = True
        client = self.client
        future = self.runner.submit(self._fetch_refresh_data(client, full))
        threading.Thread(target=self._finish_refresh,
                         args=(future, client, full, silent), daemon=True).start()

    def _schedule_auto_refresh(self) -> None:
        if self.auto_refresh.get() and self._refresh_after_id is None:
            self._refresh_after_id = self.root.after(AUTO_REFRESH_INTERVAL_MS, self._run_auto_refresh)

    def _run_auto_refresh(self) -> None:
        self._refresh_after_id = None
        if self.auto_refresh.get() and self.client is not None:
            self.refresh(full=False, silent=True)
        self._schedule_auto_refresh()

    def _on_auto_refresh_changed(self) -> None:
        if self.auto_refresh.get():
            self.refresh(full=False, silent=True)
            self._schedule_auto_refresh()
        elif self._refresh_after_id is not None:
            self.root.after_cancel(self._refresh_after_id)
            self._refresh_after_id = None

    def edit_parameter(self, _: tk.Event) -> None:
        selected = self.parameters.selection()
        if not selected:
            return
        values = self.parameters.item(selected[0], "values")
        parameter_id = int(values[0], 16)
        value = simpledialog.askinteger("写入参数", f"参数 {values[0]} 的新值", initialvalue=int(values[3]))
        if value is None:
            return
        try:
            self._call(self.client.set_parameters({parameter_id: value}))
            self.refresh()
        except Exception as error:
            messagebox.showerror("写入失败", str(error))

    def edit_ble_name(self) -> None:
        try:
            current = self._call(self.client.ble_name())
            name = simpledialog.askstring("修改蓝牙名", "输入 1–26 字节 UTF-8 名称：", initialvalue=current)
            if name is None or name == current:
                return
            if not messagebox.askyesno(
                "确认修改蓝牙名",
                "名称会立即用于 GAP 和广播数据，并写入实验室配置 Flash。\n\n继续？",
            ):
                return
            self._call(self.client.set_ble_name(name))
            self._ble_name = name
            self.refresh()
            messagebox.showinfo("蓝牙名已修改", "已更新。断开后重新扫描即可看到新名称。")
        except BmsProtocolError as error:
            messagebox.showerror("蓝牙名不可修改", f"当前固件不支持名称控制或未授权：\n{error}")
        except Exception as error:
            messagebox.showerror("修改蓝牙名失败", str(error))

    def export_parameters(self) -> None:
        if not self._parameters:
            messagebox.showwarning("没有参数", "请先刷新参数。")
            return
        path = filedialog.asksaveasfilename(defaultextension=".json", filetypes=[("JSON", "*.json")])
        if path:
            Path(path).write_text(json.dumps({f"0x{key:04X}": value for key, value in self._parameters.items()}, indent=2), encoding="utf-8")

    def import_parameters(self) -> None:
        path = filedialog.askopenfilename(filetypes=[("JSON", "*.json")])
        if not path:
            return
        try:
            source = json.loads(Path(path).read_text(encoding="utf-8"))
            values = {int(key, 0): int(value) for key, value in source.items()}
            self._call(self.client.set_parameters(values))
            self.refresh()
        except Exception as error:
            messagebox.showerror("导入/写入失败", str(error))

    def diff_parameters(self) -> None:
        if not self._parameters:
            messagebox.showwarning("没有参数", "请先刷新参数。")
            return
        path = filedialog.askopenfilename(filetypes=[("JSON", "*.json")])
        if not path:
            return
        try:
            source = json.loads(Path(path).read_text(encoding="utf-8"))
            expected = {int(key, 0): int(value) for key, value in source.items()}
            lines = []
            for parameter_id in sorted(set(self._parameters) | set(expected)):
                current = self._parameters.get(parameter_id)
                target = expected.get(parameter_id)
                if current != target:
                    lines.append(f"0x{parameter_id:04X}: 设备={current!s}，文件={target!s}")
            if not lines:
                messagebox.showinfo("参数比较", "设备参数与文件完全一致。")
                return
            dialog = tk.Toplevel(self.root)
            dialog.title("参数差异")
            dialog.minsize(520, 280)
            text = tk.Text(dialog, wrap=tk.NONE)
            text.insert(tk.END, "\n".join(lines))
            text.configure(state=tk.DISABLED)
            text.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)
        except Exception as error:
            messagebox.showerror("参数比较失败", str(error))

    def show_ota(self) -> None:
        try:
            available, layout_approved, timeout = self._call(self.client.ota_info())
            can_update = (available and layout_approved and self.client is not None and
                          isinstance(self.client.transport, BleakTransport))
            self.ota_update_button.configure(state=tk.NORMAL if can_update else tk.DISABLED)
            if not available or not layout_approved:
                next_step = ("固件目前以安全默认值拒绝 OTA；完成 Flash 分区、镜像容量、"
                             "断电恢复及实机回归后，才可将 BMS_OTA_LAYOUT_APPROVED 设为 1。")
            elif not isinstance(self.client.transport, BleakTransport):
                next_step = "演示设备不支持实际刷写；请连接真实 BLE 设备。"
            else:
                next_step = "可选择 SDK 构建且已检查过的 .bin 镜像；升级过程中请保持供电和 BLE 连接。"
            content = (f"官方 Telink OTA 服务: {'可用' if available else '不可用'}\n"
                       f"板级 Flash 布局已批准: {'是' if layout_approved else '否'}\n"
                       f"服务端超时: {timeout} 秒\n\n"
                       + next_step)
            self.ota_text.configure(state=tk.NORMAL); self.ota_text.delete("1.0", tk.END)
            self.ota_text.insert(tk.END, content); self.ota_text.configure(state=tk.DISABLED)
        except Exception as error:
            messagebox.showerror("OTA 查询失败", str(error))

    def _set_ota_progress(self, current: int, total: int) -> None:
        def apply() -> None:
            self.ota_progress.configure(maximum=total, value=current)
            self.status.set(f"OTA 传输中：{current}/{total} 块")
        try:
            self.root.after(0, apply)
        except (RuntimeError, tk.TclError):
            # 用户在传输完成前关闭窗口时，Tk 已不再接受界面任务。
            pass

    def _finish_ota(self, future) -> None:
        """等待后台 OTA 完成，再把结果交回 Tk 主线程显示。"""
        try:
            outcome = future.result()
            error = None
        except Exception as exception:
            outcome = None
            error = exception

        def apply() -> None:
            self.ota_update_button.configure(state=tk.NORMAL)
            if error is not None:
                self.status.set("OTA 失败")
                messagebox.showerror("OTA 失败", str(error))
                return
            self.status.set("OTA 完成；设备正在重启")
            messagebox.showinfo(
                "OTA 完成",
                f"已确认写入 {outcome.image_size} 字节，{outcome.block_count} 个数据块。\n\n"
                "请等待设备重启后重新连接，并确认“固件”版本已更新。",
            )

        try:
            self.root.after(0, apply)
        except (RuntimeError, tk.TclError):
            pass

    def start_ota(self) -> None:
        try:
            available, layout_approved, _ = self._call(self.client.ota_info())
            if not available or not layout_approved:
                raise RuntimeError("设备未批准 OTA Flash 布局，无法开始升级")
            path = filedialog.askopenfilename(filetypes=[("Telink 固件镜像", "*.bin")])
            if not path:
                return
            if not messagebox.askyesno(
                "确认 OTA", "此操作会向设备 Flash 写入新固件。请确认电池供电稳定且可接受升级失败风险。\n\n继续？"
            ):
                return
            self.ota_progress.configure(value=0, maximum=1)
            self.ota_update_button.configure(state=tk.DISABLED)
            self.status.set("OTA 准备传输")
            future = self.runner.submit(self.client.ota_update(path, self._set_ota_progress))
            threading.Thread(target=self._finish_ota, args=(future,), daemon=True).start()
        except Exception as error:
            self.ota_update_button.configure(state=tk.NORMAL)
            self.status.set("OTA 失败")
            messagebox.showerror("OTA 失败", str(error))

    def close(self) -> None:
        if self._refresh_after_id is not None:
            self.root.after_cancel(self._refresh_after_id)
            self._refresh_after_id = None
        if self.client is not None:
            try:
                self.runner.call(self.client.disconnect())
            except Exception:
                pass
        self.runner.close()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    BmsDesktop(root)
    root.mainloop()


if __name__ == "__main__":
    main()
