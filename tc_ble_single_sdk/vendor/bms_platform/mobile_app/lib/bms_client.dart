import 'dart:async';
import 'dart:typed_data';

import 'bms_models.dart';
import 'bmslink.dart';

abstract interface class BmsTransport {
  Stream<Uint8List> get notifications;
  Future<void> connect(String deviceId);
  Future<void> disconnect();
  Future<void> write(Uint8List data);
}

class BmsClient {
  BmsClient(this._transport);

  final BmsTransport _transport;
  final BmsLinkDecoder _decoder = BmsLinkDecoder();
  StreamSubscription<Uint8List>? _notificationSubscription;
  _PendingRequest? _pending;
  var _sequence = 1;

  Future<void> connect(String deviceId) async {
    _notificationSubscription ??= _transport.notifications.listen(_onNotification, onError: _onTransportError);
    await _transport.connect(deviceId);
  }

  Future<void> disconnect() async {
    await _transport.disconnect();
    await _notificationSubscription?.cancel();
    _notificationSubscription = null;
    _pending?.completeError(BmsProtocolException('BLE 已断开'));
    _pending = null;
  }

  Future<BmsLinkFrame> request(BmsCommand command, [List<int> payload = const <int>[]]) async {
    if (_pending != null) {
      throw StateError('BMSLink 仅允许一个未完成请求');
    }
    final sequence = _sequence;
    _sequence = sequence == 0xffff ? 1 : sequence + 1;
    final pending = _PendingRequest(sequence, command.value);
    _pending = pending;
    try {
      await _transport.write(BmsLinkFrame(sequence: sequence, command: command.value, payload: payload).encode());
      return await pending.result.future.timeout(const Duration(seconds: 3));
    } on TimeoutException {
      throw BmsProtocolException('等待 ${command.name} 响应超时');
    } finally {
      if (identical(_pending, pending)) {
        _pending = null;
      }
    }
  }

  Future<DeviceInfo> deviceInfo() async {
    return DeviceInfo.fromPayload(Uint8List.fromList((await request(BmsCommand.getDeviceInfo)).payload));
  }

  Future<RealtimeSample> realtime() async {
    return RealtimeSample.fromPayload(Uint8List.fromList((await request(BmsCommand.getRealtime)).payload));
  }

  Future<List<BmsParameter>> parameters({int startId = 0, int count = 18}) async {
    final data = ByteData(3)
      ..setUint16(0, startId, Endian.little)
      ..setUint8(2, count);
    final payload = (await request(BmsCommand.getParameters, data.buffer.asUint8List())).payload;
    if (payload.isEmpty || payload.length != 1 + payload[0] * 7) {
      throw BmsProtocolException('参数响应长度错误');
    }
    final bytes = ByteData.sublistView(Uint8List.fromList(payload));
    return List<BmsParameter>.generate(payload[0], (index) {
      final offset = 1 + index * 7;
      return BmsParameter(bytes.getUint16(offset, Endian.little), bytes.getUint8(offset + 2),
          bytes.getInt32(offset + 3, Endian.little));
    });
  }

  Future<List<BmsParameter>> allParameters() async {
    final result = <BmsParameter>[];
    var startId = 0;
    while (true) {
      final page = await parameters(startId: startId);
      result.addAll(page);
      if (page.length < 18) {
        return result;
      }
      startId = page.last.id + 1;
    }
  }

  Future<List<BmsParameterSchema>> parameterSchema({int startId = 0, int count = 7}) async {
    final data = ByteData(3)
      ..setUint16(0, startId, Endian.little)
      ..setUint8(2, count);
    final payload = (await request(BmsCommand.getParameterSchema, data.buffer.asUint8List())).payload;
    if (payload.isEmpty || payload.length != 1 + payload[0] * 16) {
      throw BmsProtocolException('参数 Schema 响应长度错误');
    }
    final bytes = ByteData.sublistView(Uint8List.fromList(payload));
    return List<BmsParameterSchema>.generate(payload[0], (index) {
      final offset = 1 + index * 16;
      return BmsParameterSchema(
        bytes.getUint16(offset, Endian.little),
        bytes.getUint8(offset + 2),
        bytes.getUint8(offset + 3),
        bytes.getInt32(offset + 4, Endian.little),
        bytes.getInt32(offset + 8, Endian.little),
        bytes.getInt32(offset + 12, Endian.little),
      );
    });
  }

  Future<List<BmsParameterSchema>> allParameterSchema() async {
    final result = <BmsParameterSchema>[];
    var startId = 0;
    while (true) {
      final page = await parameterSchema(startId: startId);
      result.addAll(page);
      if (page.length < 7) {
        return result;
      }
      startId = page.last.id + 1;
    }
  }

  Future<void> setParameters(Map<int, int> values) async {
    if (values.isEmpty || values.length > 21) {
      throw ArgumentError.value(values.length, 'values', '一次必须写入 1–21 个参数');
    }
    final data = ByteData(values.length * 6);
    var offset = 0;
    for (final entry in values.entries) {
      data.setUint16(offset, entry.key, Endian.little);
      data.setInt32(offset + 2, entry.value, Endian.little);
      offset += 6;
    }
    final response = await request(BmsCommand.setParameters, data.buffer.asUint8List());
    if (response.payload.length != 1 || response.payload[0] != values.length) {
      throw BmsProtocolException('参数写入确认错误');
    }
  }

  Future<FaultSnapshot> faults() async {
    final payload = (await request(BmsCommand.getFaults)).payload;
    if (payload.length != 12) {
      throw BmsProtocolException('故障响应长度错误');
    }
    final data = ByteData.sublistView(Uint8List.fromList(payload));
    return FaultSnapshot(data.getUint32(0, Endian.little), data.getUint32(4, Endian.little),
        data.getUint32(8, Endian.little));
  }

  Future<OtaInfo> otaInfo() async {
    final payload = (await request(BmsCommand.otaInfo)).payload;
    if (payload.length != 4) {
      throw BmsProtocolException('OTA 信息长度错误');
    }
    return OtaInfo(payload[0] != 0, payload[1] != 0, payload[2]);
  }

  void _onNotification(Uint8List fragment) {
    for (final frame in _decoder.add(fragment)) {
      final pending = _pending;
      if (pending == null || frame.sequence != pending.sequence || frame.command != pending.command) {
        continue;
      }
      if ((frame.flags & bmsLinkResponse) == 0) {
        pending.completeError(BmsProtocolException('设备返回的不是响应帧'));
      } else if ((frame.flags & bmsLinkError) != 0) {
        final code = frame.payload.isEmpty ? -1 : frame.payload.first;
        pending.completeError(BmsProtocolException('设备拒绝命令，错误码 $code'));
      } else {
        pending.complete(frame);
      }
    }
  }

  void _onTransportError(Object error, StackTrace stackTrace) {
    _pending?.completeError(error, stackTrace);
  }
}

class _PendingRequest {
  _PendingRequest(this.sequence, this.command);
  final int sequence;
  final int command;
  final Completer<BmsLinkFrame> result = Completer<BmsLinkFrame>();

  void complete(BmsLinkFrame frame) {
    if (!result.isCompleted) {
      result.complete(frame);
    }
  }

  void completeError(Object error, [StackTrace? stackTrace]) {
    if (!result.isCompleted) {
      result.completeError(error, stackTrace);
    }
  }
}
