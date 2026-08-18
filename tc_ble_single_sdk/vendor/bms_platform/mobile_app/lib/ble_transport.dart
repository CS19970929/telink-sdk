import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

import 'bms_client.dart';

const String bmsServiceUuid = 'b1a50001-a00d-4692-9144-5e8e20689457';
const String bmsRxUuid = 'b1a50001-a00d-4692-9144-5e8e20689458';
const String bmsTxUuid = 'b1a50001-a00d-4692-9144-5e8e20689459';
const int _fallbackWriteFragment = 20;

class BmsBleTransport implements BmsTransport {
  BmsBleTransport(this.ble);

  final FlutterReactiveBle ble;
  final StreamController<Uint8List> _notifications = StreamController<Uint8List>.broadcast();
  StreamSubscription<ConnectionStateUpdate>? _connectionSubscription;
  StreamSubscription<List<int>>? _notificationSubscription;
  QualifiedCharacteristic? _rx;
  QualifiedCharacteristic? _tx;
  String? _deviceId;

  @override
  Stream<Uint8List> get notifications => _notifications.stream;

  Stream<DiscoveredDevice> scan() {
    return ble.scanForDevices(
      withServices: <Uuid>[Uuid.parse(bmsServiceUuid)],
      scanMode: ScanMode.lowLatency,
    );
  }

  @override
  Future<void> connect(String deviceId) async {
    if (_deviceId != null) {
      throw StateError('已有 BLE 连接，请先断开');
    }
    final ready = Completer<void>();
    _deviceId = deviceId;
    _rx = QualifiedCharacteristic(
      serviceId: Uuid.parse(bmsServiceUuid),
      characteristicId: Uuid.parse(bmsRxUuid),
      deviceId: deviceId,
    );
    _tx = QualifiedCharacteristic(
      serviceId: Uuid.parse(bmsServiceUuid),
      characteristicId: Uuid.parse(bmsTxUuid),
      deviceId: deviceId,
    );
    _connectionSubscription = ble
        .connectToDevice(
          id: deviceId,
          servicesWithCharacteristicsToDiscover: <Uuid, List<Uuid>>{
            Uuid.parse(bmsServiceUuid): <Uuid>[Uuid.parse(bmsRxUuid), Uuid.parse(bmsTxUuid)],
          },
          connectionTimeout: const Duration(seconds: 10),
        )
        .listen((update) {
      if (update.connectionState == DeviceConnectionState.connected) {
        _notificationSubscription = ble.subscribeToCharacteristic(_tx!).listen(
          (value) => _notifications.add(Uint8List.fromList(value)),
          onError: _notifications.addError,
        );
        if (!ready.isCompleted) {
          ready.complete();
        }
      } else if (update.connectionState == DeviceConnectionState.disconnected) {
        final error = StateError('BLE 连接已断开');
        _notifications.addError(error);
        if (!ready.isCompleted) {
          ready.completeError(error);
        }
      }
    }, onError: (Object error, StackTrace stackTrace) {
      _notifications.addError(error, stackTrace);
      if (!ready.isCompleted) {
        ready.completeError(error, stackTrace);
      }
    });
    try {
      await ready.future.timeout(const Duration(seconds: 12));
    } on Object {
      await disconnect();
      rethrow;
    }
  }

  @override
  Future<void> write(Uint8List data) async {
    final characteristic = _rx;
    if (characteristic == null) {
      throw StateError('尚未连接 BMS');
    }
    for (var offset = 0; offset < data.length; offset += _fallbackWriteFragment) {
      await ble.writeCharacteristicWithoutResponse(
        characteristic,
        value: data.sublist(offset, offset + _fallbackWriteFragment > data.length
            ? data.length
            : offset + _fallbackWriteFragment),
      );
    }
  }

  @override
  Future<void> disconnect() async {
    await _notificationSubscription?.cancel();
    _notificationSubscription = null;
    await _connectionSubscription?.cancel();
    _connectionSubscription = null;
    _rx = null;
    _tx = null;
    _deviceId = null;
  }

  Future<void> dispose() async {
    await disconnect();
    await _notifications.close();
  }
}
