import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ParameterSchemaTests(unittest.TestCase):
    def test_public_schema_ids_match_firmware_ids(self) -> None:
        header = (ROOT / "include" / "bms" / "bms_parameters.h").read_text(encoding="utf-8")
        schema = (ROOT / "schema" / "bms_schema.yaml").read_text(encoding="utf-8")
        firmware_ids = {
            int(value, 16)
            for value in re.findall(r"BMS_PARAM_[A-Z0-9_]+\s*=\s*(0x[0-9A-Fa-f]+)", header)
        }
        schema_ids = {
            int(value, 16)
            for value in re.findall(r"\bid:\s*(0x[0-9A-Fa-f]+)", schema)
        }
        self.assertEqual(firmware_ids, schema_ids)


if __name__ == "__main__":
    unittest.main()
