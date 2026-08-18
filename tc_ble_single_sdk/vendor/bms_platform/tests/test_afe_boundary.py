from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AfeBoundaryTest(unittest.TestCase):
    def test_sh36735_name_stops_at_afe_and_product_configuration(self) -> None:
        for directory in (ROOT / "core", ROOT / "protocol", ROOT / "firmware", ROOT / "pc_client" / "bms_pc"):
            for source in directory.rglob("*.*"):
                if source.suffix not in {".c", ".h", ".py"}:
                    continue
                self.assertNotIn("sh36735", source.read_text(encoding="utf-8").lower(), source)

    def test_balance_mask_matches_cell_one_to_twenty_register_layout(self) -> None:
        def registers(mask: int) -> tuple[int, int, int]:
            return ((mask >> 16) & 0x0F, (mask >> 8) & 0xFF, mask & 0xFF)

        self.assertEqual(registers(1 << 0), (0x00, 0x00, 0x01))
        self.assertEqual(registers(1 << 7), (0x00, 0x00, 0x80))
        self.assertEqual(registers(1 << 8), (0x00, 0x01, 0x00))
        self.assertEqual(registers(1 << 15), (0x00, 0x80, 0x00))
        self.assertEqual(registers(1 << 16), (0x01, 0x00, 0x00))
        self.assertEqual(registers(1 << 19), (0x08, 0x00, 0x00))

    def test_raw_codes_are_not_exposed_to_the_generic_measurement_interface(self) -> None:
        driver_header = (ROOT / "include" / "bms" / "afe" / "sh36735_driver.h").read_text(encoding="utf-8")
        interface_header = (ROOT / "include" / "bms" / "afe" / "afe_interface.h").read_text(encoding="utf-8")
        self.assertIn("Sh36735RawSnapshot", driver_header)
        self.assertNotIn("Sh36735", interface_header)


if __name__ == "__main__":
    unittest.main()
