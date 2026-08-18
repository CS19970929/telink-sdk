import asyncio
from pathlib import Path
import sys
import unittest


CLIENT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CLIENT_ROOT))

from bms_pc.client import BmsClient  # noqa: E402
from bms_pc.transport import DemoTransport  # noqa: E402


class DemoClientTests(unittest.TestCase):
    def test_dashboard_parameter_and_diagnostics_flow(self) -> None:
        async def scenario() -> None:
            client = BmsClient(DemoTransport())
            await client.connect()
            info = await client.device_info()
            realtime = await client.realtime()
            parameters = await client.all_parameters()
            await client.set_parameters({0x0101: 4200})
            updated = await client.all_parameters()
            faults = await client.faults()
            ota = await client.ota_info()
            self.assertEqual(info.cell_count, 20)
            self.assertEqual(len(realtime.cells_mv), 20)
            self.assertEqual(len(parameters), 4)
            self.assertIn(4200, [item.value for item in updated])
            self.assertEqual(faults, (0, 0, 0))
            self.assertTrue(ota[0])
            await client.disconnect()

        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()
