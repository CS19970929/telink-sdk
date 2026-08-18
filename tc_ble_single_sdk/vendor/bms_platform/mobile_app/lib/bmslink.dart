import 'dart:typed_data';

const int bmsLinkVersion = 1;
const int bmsLinkMaxPayload = 128;
const int _sof0 = 0xb5;
const int _sof1 = 0x4d;
const int bmsLinkResponse = 1 << 0;
const int bmsLinkEvent = 1 << 1;
const int bmsLinkError = 1 << 2;

enum BmsCommand {
  getDeviceInfo(0x01),
  getRealtime(0x02),
  getParameters(0x10),
  setParameters(0x11),
  getParameterSchema(0x12),
  control(0x20),
  getFaults(0x30),
  getEventLog(0x31),
  otaInfo(0x40);

  const BmsCommand(this.value);
  final int value;
}

class BmsLinkFrame {
  const BmsLinkFrame({
    required this.sequence,
    required this.command,
    this.payload = const <int>[],
    this.flags = 0,
  });

  final int sequence;
  final int command;
  final List<int> payload;
  final int flags;

  Uint8List encode() {
    if (payload.length > bmsLinkMaxPayload) {
      throw ArgumentError.value(payload.length, 'payload', 'BMSLink 载荷不能超过 128 字节');
    }
    final result = Uint8List(11 + payload.length);
    final bytes = ByteData.sublistView(result);
    bytes.setUint8(0, _sof0);
    bytes.setUint8(1, _sof1);
    bytes.setUint8(2, bmsLinkVersion);
    bytes.setUint8(3, flags);
    bytes.setUint16(4, sequence, Endian.little);
    bytes.setUint8(6, command);
    bytes.setUint16(7, payload.length, Endian.little);
    result.setRange(9, 9 + payload.length, payload);
    bytes.setUint16(9 + payload.length, bmsLinkCrc16(result.sublist(0, 9 + payload.length)), Endian.little);
    return result;
  }
}

int bmsLinkCrc16(List<int> data) {
  var crc = 0xffff;
  for (final value in data) {
    crc ^= value << 8;
    for (var bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}

/// Accepts arbitrary GATT write/notify fragments and yields complete frames.
class BmsLinkDecoder {
  final List<int> _buffer = <int>[];

  List<BmsLinkFrame> add(List<int> fragment) {
    _buffer.addAll(fragment);
    final frames = <BmsLinkFrame>[];
    while (true) {
      var start = -1;
      for (var index = 0; index + 1 < _buffer.length; index++) {
        if (_buffer[index] == _sof0 && _buffer[index + 1] == _sof1) {
          start = index;
          break;
        }
      }
      if (start < 0) {
        final keepTrailingSof = _buffer.isNotEmpty && _buffer.last == _sof0;
        _buffer.clear();
        if (keepTrailingSof) {
          _buffer.add(_sof0);
        }
        return frames;
      }
      if (start > 0) {
        _buffer.removeRange(0, start);
      }
      if (_buffer.length < 9) {
        return frames;
      }
      final payloadLength = _buffer[7] | (_buffer[8] << 8);
      if (_buffer[2] != bmsLinkVersion || payloadLength > bmsLinkMaxPayload) {
        _buffer.removeAt(0);
        continue;
      }
      final frameLength = 11 + payloadLength;
      if (_buffer.length < frameLength) {
        return frames;
      }
      final body = Uint8List.fromList(_buffer.sublist(0, 9 + payloadLength));
      final receivedCrc = _buffer[9 + payloadLength] | (_buffer[10 + payloadLength] << 8);
      _buffer.removeRange(0, frameLength);
      if (bmsLinkCrc16(body) != receivedCrc) {
        continue;
      }
      final data = ByteData.sublistView(body);
      frames.add(BmsLinkFrame(
        sequence: data.getUint16(4, Endian.little),
        command: data.getUint8(6),
        payload: body.sublist(9),
        flags: data.getUint8(3),
      ));
    }
  }
}
