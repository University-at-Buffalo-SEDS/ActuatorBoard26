import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FirmwareContracts(unittest.TestCase):
    def test_schema_aliases_and_ids_match_board_header(self):
        schema = json.loads((ROOT / "config/sedsnet.json").read_text())
        header = (ROOT / "Core/Inc/sedsnet_config.h").read_text()
        aliases = {endpoint["rust"] for endpoint in schema["endpoints"]}
        self.assertEqual(len(aliases), len(schema["endpoints"]))
        self.assertTrue(all(alias.startswith("Actuator") for alias in aliases))
        for data_type in schema["types"]:
            self.assertTrue(set(data_type["endpoints"]).issubset(aliases))

        for index, endpoint in enumerate(schema["endpoints"], 100):
            self.assertRegex(
                header,
                rf"#define SEDS_EP_{re.escape(endpoint['name'])} .*{index}U",
            )
        for index, data_type in enumerate(schema["types"], 100):
            self.assertRegex(
                header,
                rf"#define SEDS_DT_{re.escape(data_type['name'])} .*{index}U",
            )

    def test_flash_partitions_are_complete_aligned_and_disjoint(self):
        config = (ROOT / "Bootloader/board_config.h").read_text()
        values = {
            name: int(value, 0)
            for name, value in re.findall(
                r"#define (ACTUATOR_[A-Z0-9_]+) (0x[0-9A-Fa-f]+)u", config
            )
        }
        regions = [
            (0x08000000, 0x4000, "bootloader"),
            (values["ACTUATOR_SLOT_A_BASE"], values["ACTUATOR_SLOT_A_SIZE"], "slot A"),
            (values["ACTUATOR_DELTA_BASE"], values["ACTUATOR_DELTA_SIZE"], "delta"),
            (values["ACTUATOR_METADATA0_BASE"], 0x800, "metadata 0"),
            (values["ACTUATOR_METADATA1_BASE"], 0x800, "metadata 1"),
            (values["ACTUATOR_PERSIST_BASE"], values["ACTUATOR_PERSIST_SIZE"], "persist"),
        ]
        self.assertEqual(regions[0][0], 0x08000000)
        for (base, size, _), (next_base, _, _) in zip(regions, regions[1:]):
            self.assertEqual(base + size, next_base)
            self.assertEqual(base % 0x800, 0)
        self.assertEqual(regions[-1][0] + regions[-1][1], 0x08080000)

        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("launchcore_generate_linker_scripts", cmake)
        self.assertIn("LAYOUT_PREFIX ACTUATOR", cmake)
        self.assertFalse((ROOT / "STM32G491XX_FLASH.ld").exists())
        self.assertFalse((ROOT / "Bootloader/linker_bootloader.ld").exists())

    def test_ota_is_delta_only_and_full_images_use_recovery(self):
        ota = (ROOT / "Core/Src/ota_stream.c").read_text()
        recovery = (ROOT / "Bootloader/recovery_transport.c").read_text()
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("launchcore_delta_update_begin", ota)
        self.assertNotIn("launchcore_recovery_install_begin", ota)
        self.assertIn("launchcore_recovery_install_begin", recovery)
        self.assertIn("RECOVERY_MAX_CHUNK 256u", recovery)
        self.assertIn("--delta-slot-size", cmake)
        self.assertIn("ACTUATOR_DELTA_SIZE 0x00006000u",
                      (ROOT / "Bootloader/board_config.h").read_text())

    def test_dependencies_are_cmake_fetched_and_pinned(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertFalse((ROOT / ".gitmodules").exists())
        self.assertIn("FetchContent_Declare(\n    sedsnet", cmake)
        self.assertIn("8821dc028718ea8e6f4231a5edad44f210581d4a", cmake)
        self.assertIn("FetchContent_Declare(\n    sedslaunchcore", cmake)
        self.assertIn("1ab6cd3dcddb7acaacb9dbfc16159f36f19363a8", cmake)

    def test_telemetry_callback_never_waits_forever_on_full_command_queue(self):
        telemetry = (ROOT / "Core/Src/telemetry.c").read_text()
        match = re.search(
            r"SedsResult Valve_Command_handler.*?\n}\n", telemetry, re.DOTALL
        )
        self.assertIsNotNone(match)
        self.assertIn("thread_comm_send(msg, TX_NO_WAIT)", match.group(0))
        self.assertNotIn("TX_WAIT_FOREVER", match.group(0))

    def test_debug_firmware_remains_size_optimized(self):
        toolchain = (ROOT / "cmake/gcc-arm-none-eabi.cmake").read_text()
        self.assertIn('set(CMAKE_C_FLAGS_DEBUG "-Os -g3")', toolchain)
        self.assertIn('set(CMAKE_CXX_FLAGS_DEBUG "-Os -g3")', toolchain)


if __name__ == "__main__":
    unittest.main()
