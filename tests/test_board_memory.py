import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class BoardMemoryTests(unittest.TestCase):
    def test_release_reuses_only_the_disabled_usb_pool(self):
        for debug, expected in ((0, 85000), (1, 69048)):
            code = (f"#define FIRMWARE_USB_DEBUG_ENABLED {debug}\n"
                    "#define TX_APP_MEM_POOL_SIZE 69048\n"
                    "#define UX_DEVICE_APP_MEM_POOL_SIZE 15952\n"
                    "#include \"board_memory_config.h\"\n"
                    f"_Static_assert(TX_APP_MEM_POOL_SIZE == {expected}, \"pool budget\");\n"
                    "int main(void) {return 0;}\n")
            with tempfile.TemporaryDirectory() as tmp:
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                "-I", str(ROOT / "Core/Inc"), "-x", "c", "-",
                                "-o", str(pathlib.Path(tmp) / "memory")],
                               input=code, text=True, check=True)
