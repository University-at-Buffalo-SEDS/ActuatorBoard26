"""The saved fill-drive selection reports a compact identity for the station's resync.

AB_FILL_DRIVE_STATUS bytes 34..35 carry a 16-bit fold of the saving station's
request time (the ms part of the FDRV record's packed stamp). 0 means no
station saved the selection: an absent record (the factory stepper default
after a wiped store) or a selection that is not loaded. The station keeps the
request times it sent, so it can tell a value it once saved (re-push) from one
it never saw (conflict).
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from router_lifecycle_fixture import function

ROOT = Path(__file__).resolve().parents[1]


def fold(ms):
    value = (ms ^ (ms >> 16) ^ (ms >> 32)) & 0xFFFF
    return value or 1


class FillDriveIdentity(unittest.TestCase):
    def test_identity_folds_the_request_ms_and_ignores_the_epoch(self):
        source = (ROOT / "Core/Src/fill_settings.c").read_text()
        harness = ("#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\n#include \"board_stamp.h\"\n"
                   + function(source, "fill_settings_identity_of") + r'''
int main(void){
 const uint64_t cases[]={1793563200000ULL,1ULL,0x10001ULL,0xFFFFFFFFFFFFULL,1793563201234ULL};
 for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){
  assert(fill_settings_identity_of(board_stamp_pack(7,cases[i]))==fill_settings_identity_of(cases[i]));
  printf("%u\n",fill_settings_identity_of(cases[i]));
 }
 assert(fill_settings_identity_of(0)==0); /* no record */
 return 0;
}
''')
        with tempfile.TemporaryDirectory(prefix="fdrv-identity-") as directory:
            exe = Path(directory) / "test"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "Core/Inc"),
                            "-x", "c", "-", "-o", str(exe)], input=harness, text=True, check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.split()
        expected = [fold(ms) for ms in (1793563200000, 1, 0x10001, 0xFFFFFFFFFFFF, 1793563201234)]
        self.assertEqual([int(v) for v in out], expected)
        self.assertEqual(expected[2], 1, "a fold of 0 reports 1, never 'no record'")

    def test_identity_follows_every_applied_record_and_is_reported(self):
        source = (ROOT / "Core/Src/fill_settings.c").read_text()
        restore = function(source, "fill_settings_restore")
        self.assertIn("selected_identity=0U", restore, "boot starts with no station record")
        self.assertRegex(restore, r"selected_cutoff=source;selected_identity=fill_settings_identity_of\(source\)")
        publish = function(source, "publish_mode")
        self.assertIn("selected_identity=fill_settings_identity_of(p->source)", publish)
        emit = function(source, "emit")
        self.assertRegex(emit, r"identity=fill_settings_identity\(\);response\[34\]=\(uint8_t\)identity;"
                               r"response\[35\]=\(uint8_t\)\(identity>>8\)")
        self.assertIn("return selected_ready?selected_identity:0U;", source)
        # The absent-record default (STEPPER) has no source: identity 0.
        self.assertTrue(re.search(r"info\.record==LAUNCHCORE_PERSIST_RECORD_ABSENT\)\{mode=FILL_MODE_STEPPER;valid=true;\}",
                                  restore))


if __name__ == "__main__":
    unittest.main()
