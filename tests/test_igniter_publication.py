"""Execute actual read-only publication, binding original DMA sample metadata."""
from pathlib import Path
import json,os,re,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "igniter_sense_math.h"
#include "board_uptime.h"
#define TELEMETRY_ENABLED 1
#define SEDS_DT_AB_IGNITER_SENSE 106291377U
typedef unsigned SedsRouter;
static uint64_t now,boot=7;static uint32_t mask,ref_mv=3300,ref_age,reported_words[12],reports;static bool ref_ok=true;
uint64_t board_uptime_ticks(void){return now;}
uint64_t board_recovery_boot_current(void){return boot;}
uint32_t __get_PRIMASK(void){return mask;}void __disable_irq(void){mask=1;}void __set_PRIMASK(uint32_t old){mask=old;}
bool board_temperature_reference_mv(uint32_t*mv,uint32_t*age){assert(!mask);*mv=ref_mv;*age=ref_age;return ref_ok;}
int seds_router_log_bytes_ex(SedsRouter*r,unsigned type,const uint8_t*p,size_t n,const void*ep,bool queue){assert(r&&type==106291377U&&n==48&&!ep&&queue&&!mask);memcpy(reported_words,p,n);++reports;return 0;}
'''
MAIN=r'''
int main(void){SedsRouter r=1;now=1000;igniter_sense_poll(&r);
 assert(reports==1&&reported_words[0]==1&&reported_words[1]==8&&reported_words[2]==7&&!reported_words[4]&&reported_words[9]==UINT32_MAX&&reported_words[11]==1);
 igniter_sense_capture(2048,1010);now=1199;igniter_sense_poll(&r);assert(reports==1);
 now=1200;igniter_sense_poll(&r);assert(reports==2&&reported_words[1]==11&&reported_words[4]==1&&reported_words[5]==1010&&reported_words[7]==2048&&reported_words[8]==3300&&reported_words[9]==190);
 now=1400;ref_age=200;igniter_sense_poll(&r);assert(reports==3&&reported_words[1]==8&&reported_words[4]==1&&reported_words[5]==1010&&reported_words[9]==390&&reported_words[10]==200);
 igniter_sense_fault();now=1600;igniter_sense_poll(&r);assert(reported_words[1]==(8|16)&&reported_words[4]==1&&reported_words[5]==1010&&reported_words[9]==590);
 igniter_sense_capture(3072,1790);now=1800;ref_ok=false;igniter_sense_poll(&r);assert(reported_words[1]==9&&reported_words[4]==2&&reported_words[7]==3072&&!reported_words[8]);
 now=(1ULL<<32)+10;igniter_sense_capture(4095,0xfffffff0U);ref_ok=true;ref_age=6001;igniter_sense_poll(&r);
 assert(reported_words[1]==(1|4|8)&&reported_words[4]==3&&reported_words[5]==0xfffffff0U&&!reported_words[6]&&reported_words[9]==26);
 assert(!mask);return 0;}
'''
class Publication(unittest.TestCase):
 def test_actual_source_times_survive_repeat_error_reference_loss_and_tick_wrap(self):
  source='\n'.join(line for line in (ROOT/'Core/Src/igniter_sense.c').read_text().splitlines()if not line.startswith('#include'))
  with tempfile.TemporaryDirectory()as d:
   for label,flags in [('native',[]),('sanitized',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
    exe=Path(d)/label
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror',*flags,'-I',str(ROOT/'Core/Inc'),'-x','c','-','-o',str(exe)],input=PRE+source+MAIN,text=True,check=True)
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:abort_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1';subprocess.run([str(exe)],env=env,check=True)
 def test_application_schema_is_hashed_unreliable_and_matches_checked_header(self):
  value=0x811c9dc5^0x5459
  for b in b'AB_IGNITER_SENSE':value=((value^b)*0x01000193)&0xffffffff
  value=0x10000|(value&0x0fffffff);self.assertEqual(value,106291377)
  schema=json.loads((ROOT/'config/sedsnet.json').read_text());entry=next(t for t in schema['types']if t['name']=='AB_IGNITER_SENSE')
  self.assertNotIn('id',entry);self.assertEqual(entry['element'],{'kind':'Static','data_type':'UInt32','count':12});self.assertEqual(entry['reliable'],'Off');self.assertEqual(entry['priority'],0)
  self.assertIn('#define SEDS_DT_AB_IGNITER_SENSE ((uint32_t)106291377U)',(ROOT/'Core/Inc/sedsnet_config.h').read_text())
if __name__=='__main__':unittest.main()
