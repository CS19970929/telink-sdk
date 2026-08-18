import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

import 'ble_transport.dart';
import 'bms_client.dart';
import 'bms_models.dart';
import 'parameter_catalog.dart';

enum MobileConnectionState { disconnected, connecting, connected, failed }

class BmsMobileState extends ChangeNotifier {
  BmsMobileState()
      : _ble = FlutterReactiveBle(),
        catalog = const ParameterCatalog.empty() {
    _transport = BmsBleTransport(_ble);
  }

  final FlutterReactiveBle _ble;
  late final BmsBleTransport _transport;
  StreamSubscription<DiscoveredDevice>? _scanSubscription;
  final Map<String, DiscoveredDevice> devices = <String, DiscoveredDevice>{};
  MobileConnectionState connectionState = MobileConnectionState.disconnected;
  bool scanning = false;
  String? error;
  BmsClient? _client;
  DeviceInfo? deviceInfo;
  RealtimeSample? realtime;
  List<BmsParameter> parameters = const <BmsParameter>[];
  Map<int, BmsParameterSchema> schemas = const <int, BmsParameterSchema>{};
  FaultSnapshot? faults;
  OtaInfo? otaInfo;
  ParameterCatalog catalog;

  Future<void> initialize() async {
    catalog = await ParameterCatalog.load();
    notifyListeners();
    await startScan();
  }

  Future<void> startScan() async {
    await _scanSubscription?.cancel();
    devices.clear();
    error = null;
    scanning = true;
    notifyListeners();
    _scanSubscription = _transport.scan().listen((device) {
      devices[device.id] = device;
      notifyListeners();
    }, onError: (Object value) {
      error = value.toString();
      scanning = false;
      notifyListeners();
    }, onDone: () {
      scanning = false;
      notifyListeners();
    });
  }

  Future<void> connect(DiscoveredDevice device) async {
    connectionState = MobileConnectionState.connecting;
    error = null;
    notifyListeners();
    try {
      final client = BmsClient(_transport);
      _client = client;
      await client.connect(device.id);
      connectionState = MobileConnectionState.connected;
      await refresh();
    } on Object catch (value) {
      error = value.toString();
      connectionState = MobileConnectionState.failed;
      _client = null;
    }
    notifyListeners();
  }

  Future<void> refresh() async {
    final client = _requireClient();
    try {
      deviceInfo = await client.deviceInfo();
      realtime = await client.realtime();
      parameters = await client.allParameters();
      schemas = <int, BmsParameterSchema>{for (final value in await client.allParameterSchema()) value.id: value};
      faults = await client.faults();
      otaInfo = await client.otaInfo();
      error = null;
    } on Object catch (value) {
      error = value.toString();
    }
    notifyListeners();
  }

  Future<void> writeParameter(int id, int value) async {
    final schema = schemas[id];
    if (schema != null && (value < schema.minimum || value > schema.maximum)) {
      error = '参数 0x${id.toRadixString(16)} 必须在 ${schema.minimum}..${schema.maximum} 范围内';
      notifyListeners();
      return;
    }
    try {
      await _requireClient().setParameters(<int, int>{id: value});
      parameters = await _requireClient().allParameters();
      error = null;
    } on Object catch (failure) {
      error = failure.toString();
    }
    notifyListeners();
  }

  Future<void> disconnect() async {
    await _client?.disconnect();
    _client = null;
    connectionState = MobileConnectionState.disconnected;
    deviceInfo = null;
    realtime = null;
    parameters = const <BmsParameter>[];
    schemas = const <int, BmsParameterSchema>{};
    faults = null;
    otaInfo = null;
    notifyListeners();
  }

  BmsClient _requireClient() {
    final client = _client;
    if (client == null || connectionState != MobileConnectionState.connected) {
      throw StateError('尚未连接 BMS');
    }
    return client;
  }

  @override
  void dispose() {
    _scanSubscription?.cancel();
    _transport.dispose();
    super.dispose();
  }
}
