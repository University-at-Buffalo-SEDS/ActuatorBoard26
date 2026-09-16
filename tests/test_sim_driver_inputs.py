import json
from pathlib import Path
import unittest


class DriverInputContracts(unittest.TestCase):
    def test_healthy_active_low_fault_inputs_are_externally_high(self):
        root = Path(__file__).resolve().parents[1]
        layout = json.loads((root / "sim/board.json").read_text())
        pins = {pin["pin"]: pin["initial"] for pin in layout["board"]["pins"]}
        headers = (root / "Core/Inc/main.h").read_text()
        for name, pin in [("H_BRIDGE_FAULT", 0), ("N20_FAULT", 1),
                          ("N2_FAULT", 2), ("IGNITER_FAULT", 3)]:
            self.assertIn(f"#define {name}_Pin GPIO_PIN_{pin}", headers)
            self.assertIn(f"#define {name}_GPIO_Port GPIOC", headers)
            self.assertEqual(pins[32 + pin], "high")
