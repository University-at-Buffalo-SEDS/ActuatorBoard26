import json
import unittest
from pathlib import Path

import build


class QualificationContractTests(unittest.TestCase):
    def test_full_runner_profiles_memory_and_linked_network(self):
        root = Path(build.__file__).resolve().parent
        runner = (root / "sim" / "run_full.py").read_text(encoding="utf-8")
        script = (root / "build.py").read_text(encoding="utf-8")

        self.assertIn('"profile"', runner)
        self.assertIn('"--sample-count", "20"', runner)
        self.assertEqual(runner.count('str(max(1000, layout["execution"]["virtual_time_ms"]))'), 2)
        self.assertIn('"--traffic-iterations", "1000000"', runner)
        self.assertIn('"bay"', runner)
        self.assertIn('"tx_probe": "fdcan_tx_ok"', runner)
        self.assertIn('"rx_probe": "fdcan_rx"', runner)
        self.assertIn('"host_nodes"', runner)
        self.assertIn('"groundstation"', runner)
        self.assertIn('"rocket_radio"', runner)
        self.assertIn('"fill_pico"', runner)
        self.assertIn('"GS_SIM_VALIDATE_VALVE_ROUNDTRIP": "1"', runner)
        self.assertIn('"GS_SIM_VALIDATE_SOAK_COMMANDS": "1" if ultra_soak else "0"', runner)
        self.assertIn("Valve command path remained alive during soak interval", runner)
        self.assertIn("Every ten-minute soak command returned an acknowledgement", runner)
        self.assertIn('"probe": "valve_commands_received", "minimum": 1', runner)
        self.assertIn("Valve acknowledgement completed the routed return path", runner)
        self.assertIn('"GroundStation discovered every board by autonomous name"', runner)
        self.assertIn('"transport_path": ["Gateway", "Actuator", "Valve", "DAQ"]', runner)
        self.assertIn('simulation_env["SEDS_FIRMWARE_SIM_TEST"] = "1"', runner)
        self.assertIn('run_live(command, "firmware simulation")', runner)
        self.assertIn('running ({int(now - started)}s elapsed)', runner)
        self.assertIn("Long-duration memory profile", script)
        self.assertIn("Network discovery and time sync", script)

    def test_layout_exposes_network_convergence(self):
        root = Path(build.__file__).resolve().parent
        layout = json.loads((root / "sim" / "board.json").read_text(encoding="utf-8"))
        self.assertLess(layout["execution"].get("memory_probe_warmup_samples", 0), layout["execution"]["sample_count"])
        probes = {
            probe["name"]: probe["symbol"]
            for probe in layout["execution"]["memory_probes"]
        }
        self.assertEqual(probes["network_ready"], "g_telemetry_network_ready")
        self.assertEqual(probes["discovery_seen"], "g_telemetry_discovery_seen")
        self.assertEqual(probes["timesync_valid"], "g_telemetry_timesync_valid")
        self.assertEqual(probes["telemetry_stack_remaining"],
                         "g_telemetry_stack_remaining")

        telemetry = (root / "Core" / "Src" / "telemetry.c").read_text(encoding="utf-8")
        telemetry_thread = (root / "Core" / "Src" / "telemetry_thread.c").read_text(encoding="utf-8")
        self.assertIn("g_telemetry_stack_remaining", telemetry_thread)
        for symbol in (
            "g_telemetry_network_ready",
            "g_telemetry_discovery_seen",
            "g_telemetry_timesync_valid",
        ):
            self.assertIn(symbol, telemetry)

    def test_shared_can_avoids_hop_retry_storms(self):
        root = Path(build.__file__).resolve().parent
        telemetry = (root / "Core" / "Src" / "telemetry.c").read_text(encoding="utf-8")
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("seds_router_add_side_packed_profile(", telemetry)
        self.assertIn("SEDS_SIDE_TRANSPORT_PROFILE_IPV6_LIKE", telemetry)
        can_bus = (root / "Core" / "Src" / "can_bus.c").read_text(encoding="utf-8")
        self.assertIn("can_bus_wait_for_tx_slot", can_bus)
        self.assertIn("CAN_BUS_TX_ENQUEUE_TIMEOUT_MS 5U", can_bus)
        self.assertNotIn("< (uint32_t)frag_cnt", can_bus)
        self.assertIn("BOARD_CAN_MAX_FRAME_BYTES 128U", telemetry)
        self.assertIn('SEDSNET_MAX_QUEUE_BUDGET "8192"', cmake)
        self.assertNotIn("seds_router_export_topology_len", telemetry)
        self.assertIn("g_telemetry_discovery_seen = 1U", telemetry)


if __name__ == "__main__":
    unittest.main()
