from pathlib import Path
import struct
import sys
import tempfile
import unittest


CLIENT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CLIENT_ROOT))

from bms_pc.ota import (  # noqa: E402
    TELINK_OTA_END,
    load_firmware_image,
    make_ota_data_packet,
    make_ota_end_packet,
    telink_ota_crc16,
)


class TelinkOtaTests(unittest.TestCase):
    def _image_path(self) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "telink_bms.bin"
        image = bytearray(range(64))
        struct.pack_into("<I", image, 0x18, 35)
        path.write_bytes(image)
        return path

    def test_crc16_has_standard_vector(self) -> None:
        self.assertEqual(telink_ota_crc16(b"123456789"), 0x4B37)

    def test_packets_follow_legacy_16_byte_layout(self) -> None:
        image = load_firmware_image(self._image_path())
        self.assertEqual(image.declared_size, 35)
        self.assertEqual(image.block_count, 3)
        packet = make_ota_data_packet(image, 2)
        self.assertEqual(len(packet), 20)
        self.assertEqual(packet[:2], b"\x02\x00")
        self.assertEqual(packet[5:18], b"\xff" * 13)
        self.assertEqual(struct.unpack_from("<H", packet, 18)[0], telink_ota_crc16(packet[:18]))
        self.assertEqual(make_ota_end_packet(image), struct.pack("<HHH", TELINK_OTA_END, 2, 0xFFFD))


if __name__ == "__main__":
    unittest.main()
