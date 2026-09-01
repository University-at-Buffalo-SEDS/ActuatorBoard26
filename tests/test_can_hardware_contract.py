import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class CanHardwareContract(unittest.TestCase):
    def test_uses_known_good_fill_bus_timing(self):
        source = (ROOT / "Core" / "Src" / "main.c").read_text(encoding="utf-8")
        ioc = (ROOT / "ActuationBoard.ioc").read_text(encoding="utf-8")

        self.assertIn("hfdcan2.Init.AutoRetransmission = ENABLE", source)
        self.assertIn("hfdcan2.Init.NominalPrescaler = 16", source)
        self.assertIn("hfdcan2.Init.NominalTimeSeg1 = 1", source)
        self.assertIn("hfdcan2.Init.NominalTimeSeg2 = 1", source)
        self.assertIn("FDCAN2.CalculateBaudRateNominal=3541666", ioc)
        self.assertIn("FDCAN2.AutoRetransmission=ENABLE", ioc)


if __name__ == "__main__":
    unittest.main()
