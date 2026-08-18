import unittest


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class BmsLinkVectorTest(unittest.TestCase):
    def test_ccitt_reference_vector(self) -> None:
        self.assertEqual(crc16_ccitt(b"123456789"), 0x29B1)

    def test_get_device_info_frame(self) -> None:
        header = bytes((0xB5, 0x4D, 0x01, 0x00, 0x34, 0x12, 0x01, 0x00, 0x00))
        frame = header + crc16_ccitt(header).to_bytes(2, "little")
        self.assertEqual(frame, bytes.fromhex("B5 4D 01 00 34 12 01 00 00 CC 78"))


if __name__ == "__main__":
    unittest.main()
