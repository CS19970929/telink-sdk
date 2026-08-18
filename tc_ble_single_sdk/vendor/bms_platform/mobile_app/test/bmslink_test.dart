import 'package:flutter_test/flutter_test.dart';
import 'package:telink_bms_mobile/bmslink.dart';

void main() {
  test('CRC-16/CCITT-FALSE reference vector', () {
    expect(bmsLinkCrc16('123456789'.codeUnits), 0x29b1);
  });

  test('device-info request has the shared fixed vector', () {
    final frame = BmsLinkFrame(sequence: 0x1234, command: BmsCommand.getDeviceInfo.value).encode();
    expect(frame, <int>[0xb5, 0x4d, 1, 0, 0x34, 0x12, 1, 0, 0, 0xcc, 0x78]);
  });

  test('decoder restores a frame from arbitrary GATT fragments', () {
    final encoded = BmsLinkFrame(sequence: 7, command: BmsCommand.getRealtime.value, payload: <int>[1, 2, 3]).encode();
    final decoder = BmsLinkDecoder();
    expect(decoder.add(encoded.sublist(0, 2)), isEmpty);
    expect(decoder.add(encoded.sublist(2, 8)), isEmpty);
    final frames = decoder.add(encoded.sublist(8));
    expect(frames, hasLength(1));
    expect(frames.single.sequence, 7);
    expect(frames.single.payload, <int>[1, 2, 3]);
  });
}
