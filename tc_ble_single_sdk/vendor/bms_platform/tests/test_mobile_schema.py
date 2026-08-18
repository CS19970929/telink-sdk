from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MobileSchemaTest(unittest.TestCase):
    def test_mobile_bundle_matches_public_parameter_schema(self) -> None:
        public_schema = ROOT / "schema" / "bms_schema.yaml"
        mobile_schema = ROOT / "mobile_app" / "assets" / "bms_schema.yaml"
        self.assertEqual(mobile_schema.read_bytes(), public_schema.read_bytes())

    def test_mobile_client_uses_bmslink_not_afe_registers(self) -> None:
        source_root = ROOT / "mobile_app" / "lib"
        source = "\n".join(path.read_text(encoding="utf-8") for path in source_root.glob("*.dart"))
        self.assertIn("BmsLinkFrame", source)
        self.assertIn("getParameterSchema", source)
        self.assertNotIn("SH36735", source)


if __name__ == "__main__":
    unittest.main()
