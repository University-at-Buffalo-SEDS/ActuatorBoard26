import binascii
import importlib.util
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "launchcore_recover", ROOT / "tools/launchcore_recover.py"
)
RECOVERY = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RECOVERY)


class RecoveryProtocolTests(unittest.TestCase):
    def test_frame_is_little_endian_bounded_and_crc_protected(self):
        payload = bytes(range(32))
        frame = RECOVERY.encode_frame(RECOVERY.OP_DATA, 0x100, payload)
        magic, version, opcode, length, offset, checksum = RECOVERY.HEADER.unpack(
            frame[: RECOVERY.HEADER.size]
        )
        self.assertEqual(magic, RECOVERY.MAGIC)
        self.assertEqual(version, RECOVERY.VERSION)
        self.assertEqual(opcode, RECOVERY.OP_DATA)
        self.assertEqual(length, len(payload))
        self.assertEqual(offset, 0x100)
        self.assertEqual(checksum, binascii.crc32(frame[:12] + payload) & 0xFFFFFFFF)

    def test_chunk_size_matches_bootloader_bound(self):
        source = (ROOT / "Bootloader/recovery_transport.c").read_text()
        self.assertIn(f"RECOVERY_MAX_CHUNK {RECOVERY.MAX_CHUNK}u", source)


if __name__ == "__main__":
    unittest.main()
