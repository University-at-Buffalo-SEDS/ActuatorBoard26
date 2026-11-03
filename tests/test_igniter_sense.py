"""Read-only current-sense math; no output commands or hardware adapter."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class IgniterSense(unittest.TestCase):
    def test_signal_reference_and_no_invented_current(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include "igniter_sense_math.h"
int main(void) {
 uint32_t mv=7;
 assert(igniter_sense_vdda_mv(1500,1650,3000,&mv)&&mv==3300);
 assert(!igniter_sense_vdda_mv(0,1650,3000,&mv)&&mv==3300);
 assert(!igniter_sense_vdda_mv(4095,1650,3000,&mv));
 assert(!igniter_sense_vdda_mv(1500,65535,3000,&mv));
 assert(!igniter_sense_vdda_mv(1,1650,3000,&mv));
 assert(!igniter_sense_vdda_mv(4094,1,3000,&mv));
 assert(igniter_sense_vdda_mv(3000,1620,3000,&mv)&&mv==1620);
 assert(igniter_sense_vdda_mv(1500,1800,3000,&mv)&&mv==3600);
 assert(!igniter_sense_vdda_mv(3000,1619,3000,&mv));
 assert(!igniter_sense_vdda_mv(3000,3601,3000,&mv));
 assert(!(igniter_sense_flags(2048,0,true,1,0,true,false)&IGNITER_SENSE_FLAG_REFERENCE_VALID));
 assert(!igniter_sense_vdda_mv(1500,1650,3000,0));
 uint32_t good=igniter_sense_flags(2048,0,true,3300,0,true,false);
 assert(good==(IGNITER_SENSE_FLAG_SAMPLE_VALID|IGNITER_SENSE_FLAG_REFERENCE_VALID|IGNITER_SENSE_FLAG_CURRENT_UNSUPPORTED));
 assert(igniter_sense_flags(0,0,true,3300,0,true,false)&IGNITER_SENSE_FLAG_SATURATED);
 assert(igniter_sense_flags(4095,0,true,3300,0,true,false)&IGNITER_SENSE_FLAG_SATURATED);
 assert(!(igniter_sense_flags(4096,0,true,3300,0,true,false)&IGNITER_SENSE_FLAG_SAMPLE_VALID));
 assert(!(igniter_sense_flags(2048,251,true,3300,0,true,false)&IGNITER_SENSE_FLAG_SAMPLE_VALID));
 assert(!(igniter_sense_flags(2048,0,false,3300,0,true,false)&IGNITER_SENSE_FLAG_SAMPLE_VALID));
 assert(!(igniter_sense_flags(2048,0,true,3300,6001,true,false)&IGNITER_SENSE_FLAG_REFERENCE_VALID));
 assert(!(igniter_sense_flags(2048,0,true,3300,0,false,false)&IGNITER_SENSE_FLAG_REFERENCE_VALID));
 assert(!(igniter_sense_flags(2048,0,true,3300,0,true,true)&IGNITER_SENSE_FLAG_SAMPLE_VALID));
 assert(igniter_sense_flags(2048,250,true,3300,6000,true,false)==good);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            for label,flags in [('native',[]),('sanitized',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
                exe=Path(directory)/label
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',*flags,'-I',str(ROOT/'Core/Inc'),'-x','c','-','-o',str(exe)],input=code,text=True,check=True)
                env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:abort_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1'
                subprocess.run([str(exe)],env=env,check=True)

if __name__=='__main__': unittest.main()
