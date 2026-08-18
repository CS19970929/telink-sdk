import 'dart:typed_data';

class BmsProtocolException implements Exception {
  BmsProtocolException(this.message);
  final String message;

  @override
  String toString() => 'BMS 协议错误：$message';
}

class DeviceInfo {
  const DeviceInfo({
    required this.firmware,
    required this.mcu,
    required this.afeKind,
    required this.cellCount,
    required this.temperatureCount,
    required this.powerTopology,
    required this.capabilities,
  });

  factory DeviceInfo.fromPayload(Uint8List payload) {
    if (payload.length != 12) {
      throw BmsProtocolException('设备信息长度错误');
    }
    final data = ByteData.sublistView(payload);
    return DeviceInfo(
      firmware: (payload[0], payload[1], payload[2]),
      mcu: payload[3],
      afeKind: payload[4],
      cellCount: payload[5],
      temperatureCount: payload[6],
      powerTopology: payload[7],
      capabilities: data.getUint32(8, Endian.little),
    );
  }

  final (int, int, int) firmware;
  final int mcu;
  final int afeKind;
  final int cellCount;
  final int temperatureCount;
  final int powerTopology;
  final int capabilities;
}

class RealtimeSample {
  const RealtimeSample({
    required this.validFlags,
    required this.timestampMs,
    required this.packVoltageMv,
    required this.currentMa,
    required this.powerMw,
    required this.socPermil,
    required this.sohPermil,
    required this.cellsMv,
    required this.temperaturesDecic,
    required this.balanceMask,
    required this.alarmFlags,
    required this.protectionFlags,
    required this.faultFlags,
    required this.stateFlags,
  });

  factory RealtimeSample.fromPayload(Uint8List payload) {
    if (payload.length < 43) {
      throw BmsProtocolException('实时数据长度错误');
    }
    final data = ByteData.sublistView(payload);
    final cellCount = data.getUint8(24);
    final temperatureCount = data.getUint8(25);
    final requiredLength = 26 + cellCount * 2 + temperatureCount * 2 + 17;
    if (payload.length != requiredLength) {
      throw BmsProtocolException('实时数组长度错误');
    }
    var offset = 26;
    final cells = <int>[];
    for (var index = 0; index < cellCount; index++) {
      cells.add(data.getUint16(offset, Endian.little));
      offset += 2;
    }
    final temperatures = <int>[];
    for (var index = 0; index < temperatureCount; index++) {
      temperatures.add(data.getInt16(offset, Endian.little));
      offset += 2;
    }
    final result = RealtimeSample(
      validFlags: data.getUint32(0, Endian.little),
      timestampMs: data.getUint32(4, Endian.little),
      packVoltageMv: data.getUint32(8, Endian.little),
      currentMa: data.getInt32(12, Endian.little),
      powerMw: data.getInt32(16, Endian.little),
      socPermil: data.getUint16(20, Endian.little),
      sohPermil: data.getUint16(22, Endian.little),
      cellsMv: cells,
      temperaturesDecic: temperatures,
      balanceMask: data.getUint32(offset, Endian.little),
      alarmFlags: data.getUint32(offset + 4, Endian.little),
      protectionFlags: data.getUint32(offset + 8, Endian.little),
      faultFlags: data.getUint32(offset + 12, Endian.little),
      stateFlags: data.getUint8(offset + 16),
    );
    return result;
  }

  final int validFlags;
  final int timestampMs;
  final int packVoltageMv;
  final int currentMa;
  final int powerMw;
  final int socPermil;
  final int sohPermil;
  final List<int> cellsMv;
  final List<int> temperaturesDecic;
  final int balanceMask;
  final int alarmFlags;
  final int protectionFlags;
  final int faultFlags;
  final int stateFlags;

  int get cellMinMv => cellsMv.isEmpty ? 0 : cellsMv.reduce((a, b) => a < b ? a : b);
  int get cellMaxMv => cellsMv.isEmpty ? 0 : cellsMv.reduce((a, b) => a > b ? a : b);
  int get cellDeltaMv => cellMaxMv - cellMinMv;
}

class BmsParameter {
  const BmsParameter(this.id, this.type, this.value);
  final int id;
  final int type;
  final int value;
}

class BmsParameterSchema {
  const BmsParameterSchema(this.id, this.type, this.flags, this.minimum, this.maximum, this.defaultValue);
  final int id;
  final int type;
  final int flags;
  final int minimum;
  final int maximum;
  final int defaultValue;
}

class FaultSnapshot {
  const FaultSnapshot(this.alarmFlags, this.protectionFlags, this.faultFlags);
  final int alarmFlags;
  final int protectionFlags;
  final int faultFlags;
}

class OtaInfo {
  const OtaInfo(this.available, this.transferViaBmsLink, this.timeoutSeconds);
  final bool available;
  final bool transferViaBmsLink;
  final int timeoutSeconds;
}
