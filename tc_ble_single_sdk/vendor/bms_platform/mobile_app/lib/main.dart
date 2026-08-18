import 'package:flutter/material.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

import 'app_state.dart';
import 'bms_models.dart';

void main() {
  runApp(const BmsMobileApp());
}

class BmsMobileApp extends StatefulWidget {
  const BmsMobileApp({super.key});

  @override
  State<BmsMobileApp> createState() => _BmsMobileAppState();
}

class _BmsMobileAppState extends State<BmsMobileApp> {
  final BmsMobileState _state = BmsMobileState();
  var _tab = 0;

  @override
  void initState() {
    super.initState();
    _state.addListener(_onStateChanged);
    _state.initialize();
  }

  @override
  void dispose() {
    _state
      ..removeListener(_onStateChanged)
      ..dispose();
    super.dispose();
  }

  void _onStateChanged() {
    if (mounted) {
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    final pages = <Widget>[
      _DevicesPage(state: _state),
      _DashboardPage(state: _state),
      _CellsPage(state: _state),
      _ParametersPage(state: _state),
      _DiagnosticsPage(state: _state),
    ];
    return MaterialApp(
      title: 'Telink BMS',
      theme: ThemeData(colorSchemeSeed: Colors.teal, useMaterial3: true),
      home: Scaffold(
        appBar: AppBar(
          title: const Text('Telink BMS'),
          actions: <Widget>[
            if (_state.connectionState == MobileConnectionState.connected)
              IconButton(onPressed: _state.disconnect, icon: const Icon(Icons.link_off), tooltip: '断开连接'),
          ],
        ),
        body: Column(
          children: <Widget>[
            if (_state.error != null)
              MaterialBanner(
                content: Text(_state.error!),
                actions: <Widget>[TextButton(onPressed: () => _state.refresh(), child: const Text('重试'))],
              ),
            Expanded(child: pages[_tab]),
          ],
        ),
        bottomNavigationBar: NavigationBar(
          selectedIndex: _tab,
          onDestinationSelected: (index) => setState(() => _tab = index),
          destinations: const <NavigationDestination>[
            NavigationDestination(icon: Icon(Icons.bluetooth_searching), label: '设备'),
            NavigationDestination(icon: Icon(Icons.dashboard), label: '概览'),
            NavigationDestination(icon: Icon(Icons.grid_view), label: '电芯'),
            NavigationDestination(icon: Icon(Icons.tune), label: '参数'),
            NavigationDestination(icon: Icon(Icons.health_and_safety), label: '诊断'),
          ],
        ),
      ),
    );
  }
}

class _DevicesPage extends StatelessWidget {
  const _DevicesPage({required this.state});
  final BmsMobileState state;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: <Widget>[
        ListTile(
          title: Text(state.scanning ? '正在扫描 BMS 服务…' : '发现 ${state.devices.length} 台 BMS'),
          trailing: FilledButton.icon(
            onPressed: state.startScan,
            icon: const Icon(Icons.refresh),
            label: const Text('扫描'),
          ),
        ),
        Expanded(
          child: ListView(
            children: state.devices.values.map((device) => _DeviceTile(device: device, state: state)).toList(),
          ),
        ),
      ],
    );
  }
}

class _DeviceTile extends StatelessWidget {
  const _DeviceTile({required this.device, required this.state});
  final DiscoveredDevice device;
  final BmsMobileState state;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      leading: const Icon(Icons.battery_full),
      title: Text(device.name.isEmpty ? '未命名 Telink BMS' : device.name),
      subtitle: Text('${device.id}\nRSSI ${device.rssi} dBm'),
      isThreeLine: true,
      trailing: FilledButton(
        onPressed: state.connectionState == MobileConnectionState.connecting ? null : () => state.connect(device),
        child: const Text('连接'),
      ),
    );
  }
}

class _DashboardPage extends StatelessWidget {
  const _DashboardPage({required this.state});
  final BmsMobileState state;

  @override
  Widget build(BuildContext context) {
    final realtime = state.realtime;
    if (realtime == null) {
      return const Center(child: Text('连接设备后显示实时数据'));
    }
    return RefreshIndicator(
      onRefresh: state.refresh,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: <Widget>[
          Text('SOC ${(realtime.socPermil / 10).toStringAsFixed(1)}%', style: Theme.of(context).textTheme.headlineMedium),
          const SizedBox(height: 16),
          _MetricCard(label: '总压', value: '${realtime.packVoltageMv} mV'),
          _MetricCard(label: '电流', value: '${realtime.currentMa} mA（放电为正）'),
          _MetricCard(label: '功率', value: '${realtime.powerMw} mW'),
          _MetricCard(label: '电芯压差', value: '${realtime.cellDeltaMv} mV'),
          _MetricCard(label: '均衡掩码', value: '0x${realtime.balanceMask.toRadixString(16).padLeft(8, '0')}'),
        ],
      ),
    );
  }
}

class _MetricCard extends StatelessWidget {
  const _MetricCard({required this.label, required this.value});
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Card(child: ListTile(title: Text(label), trailing: Text(value)));
  }
}

class _CellsPage extends StatelessWidget {
  const _CellsPage({required this.state});
  final BmsMobileState state;

  @override
  Widget build(BuildContext context) {
    final realtime = state.realtime;
    if (realtime == null) {
      return const Center(child: Text('暂无电芯与温度数据'));
    }
    return RefreshIndicator(
      onRefresh: state.refresh,
      child: ListView(
        padding: const EdgeInsets.all(12),
        children: <Widget>[
          Text('电芯 (${realtime.cellsMv.length}S)', style: Theme.of(context).textTheme.titleLarge),
          GridView.count(
            crossAxisCount: 2,
            physics: const NeverScrollableScrollPhysics(),
            shrinkWrap: true,
            childAspectRatio: 2.3,
            children: List<Widget>.generate(realtime.cellsMv.length, (index) {
              return Card(child: Center(child: Text('CELL ${index + 1}: ${realtime.cellsMv[index]} mV')));
            }),
          ),
          const SizedBox(height: 16),
          Text('温度', style: Theme.of(context).textTheme.titleLarge),
          ...List<Widget>.generate(realtime.temperaturesDecic.length, (index) => ListTile(
                leading: const Icon(Icons.thermostat),
                title: Text('TS ${index + 1}'),
                trailing: Text('${(realtime.temperaturesDecic[index] / 10).toStringAsFixed(1)} °C'),
              )),
        ],
      ),
    );
  }
}

class _ParametersPage extends StatelessWidget {
  const _ParametersPage({required this.state});
  final BmsMobileState state;

  @override
  Widget build(BuildContext context) {
    if (state.parameters.isEmpty) {
      return const Center(child: Text('连接设备后读取参数 Schema'));
    }
    return RefreshIndicator(
      onRefresh: state.refresh,
      child: ListView(
        children: state.parameters.map((parameter) {
          final metadata = state.catalog.metadataFor(parameter.id);
          final schema = state.schemas[parameter.id];
          return ListTile(
            title: Text(metadata.key),
            subtitle: Text('0x${parameter.id.toRadixString(16).padLeft(4, '0')}  范围 ${schema?.minimum ?? '-'}..${schema?.maximum ?? '-'} ${metadata.unit}'),
            trailing: SizedBox(
              width: 120,
              child: TextFormField(
                initialValue: '${parameter.value}',
                enabled: schema != null && (schema.flags & 0x02) != 0,
                keyboardType: TextInputType.number,
                textInputAction: TextInputAction.done,
                onFieldSubmitted: (value) {
                  final parsed = int.tryParse(value);
                  if (parsed != null) {
                    state.writeParameter(parameter.id, parsed);
                  }
                },
                decoration: InputDecoration(suffixText: metadata.unit),
              ),
            ),
          );
        }).toList(),
      ),
    );
  }
}

class _DiagnosticsPage extends StatelessWidget {
  const _DiagnosticsPage({required this.state});
  final BmsMobileState state;

  @override
  Widget build(BuildContext context) {
    final info = state.deviceInfo;
    final fault = state.faults;
    final ota = state.otaInfo;
    if (info == null) {
      return const Center(child: Text('连接设备后显示诊断信息'));
    }
    return RefreshIndicator(
      onRefresh: state.refresh,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: <Widget>[
          _MetricCard(label: '固件', value: '${info.firmware.$1}.${info.firmware.$2}.${info.firmware.$3}'),
          _MetricCard(label: 'MCU / AFE', value: '0x${info.mcu.toRadixString(16)} / ${info.afeKind}'),
          _MetricCard(label: '报警', value: '0x${(fault?.alarmFlags ?? 0).toRadixString(16).padLeft(8, '0')}'),
          _MetricCard(label: '保护', value: '0x${(fault?.protectionFlags ?? 0).toRadixString(16).padLeft(8, '0')}'),
          _MetricCard(label: '故障', value: '0x${(fault?.faultFlags ?? 0).toRadixString(16).padLeft(8, '0')}'),
          _MetricCard(label: 'OTA 服务', value: ota?.available == true ? '已发现；超时 ${ota!.timeoutSeconds} 秒' : '未发现'),
          const Padding(
            padding: EdgeInsets.only(top: 16),
            child: Text('实际 OTA 数据传输走 Telink 官方 OTA Service，不走 BMSLink。未确认 Flash 分区和断电恢复前，移动端不提供刷写按钮。'),
          ),
        ],
      ),
    );
  }
}
