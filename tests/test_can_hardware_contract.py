import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class CanHardwareContract(unittest.TestCase):
    def test_preserves_bus_rate_with_later_sample_point(self):
        source = (ROOT / "Core" / "Src" / "main.c").read_text(encoding="utf-8")
        ioc = (ROOT / "ActuationBoard.ioc").read_text(encoding="utf-8")

        self.assertIn("hfdcan2.Init.AutoRetransmission = ENABLE", source)
        self.assertIn("hfdcan2.Init.NominalPrescaler = 2", source)
        self.assertIn("hfdcan2.Init.NominalTimeSeg1 = 19", source)
        self.assertIn("hfdcan2.Init.NominalTimeSeg2 = 4", source)
        self.assertIn("hfdcan2.Init.NominalSyncJumpWidth = 4", source)
        self.assertEqual(2 * (1 + 19 + 4), 16 * (1 + 1 + 1))
        self.assertAlmostEqual((1 + 19) / (1 + 19 + 4), 5 / 6)
        self.assertIn("FDCAN2.NominalPrescaler=2", ioc)
        self.assertIn("FDCAN2.NominalTimeSeg1=19", ioc)
        self.assertIn("FDCAN2.NominalTimeSeg2=4", ioc)
        self.assertIn("FDCAN2.NominalSyncJumpWidth=4", ioc)
        self.assertIn("FDCAN2.CalculateBaudRateNominal=3541666", ioc)
        self.assertIn("FDCAN2.AutoRetransmission=ENABLE", ioc)

    def test_large_packets_stream_through_the_bounded_hardware_fifo(self):
        can = (ROOT / "Core/Src/can_bus.c").read_text()
        self.assertNotIn("can_bus_wait_for_tx_slot", can)
        self.assertIn("return can_tx_queue_submit(bytes, len, std_id);", can)
        self.assertNotIn(
            "HAL_FDCAN_GetTxFifoFreeLevel(g_hfdcan) < (uint32_t)frag_cnt",
            can,
        )


if __name__ == "__main__":
    unittest.main()
