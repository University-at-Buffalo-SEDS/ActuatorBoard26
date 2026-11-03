"""Pure approved motion-provenance lifecycle and explicit56-byte wire codec."""
from pathlib import Path
import ctypes,json,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class MotionStatusV2Tests(unittest.TestCase):
 def test_actual_elapsed_wrap_cancellation_and_invalid_origins(self):
  code=r'''
#include "fill_motion_status.h"
#include "fill_motion.h"
#include <assert.h>
#include <stdint.h>
int main(void){FillMotionProvenance p={0};assert(fill_motion_provenance_remaining(&p,45000)==45000);
 fill_motion_provenance_start(&p,10000,2,FILL_ACTION_RETRACT,UINT32_MAX-1000U);assert(p.lifecycle==1);
 assert(fill_motion_provenance_remaining(&p,UINT32_MAX-1000U)==45000);assert(fill_motion_provenance_remaining(&p,(uint32_t)(UINT32_MAX-1000U+44999U))==1);assert(fill_motion_provenance_remaining(&p,(uint32_t)(UINT32_MAX-1000U+45000U))==0);
 fill_motion_provenance_complete(&p);assert(p.lifecycle==2&&fill_motion_provenance_remaining(&p,0)==0); /* satisfied never rolls back */
 fill_motion_provenance_cancel(&p,false);assert(p.lifecycle==3&&fill_motion_provenance_remaining(&p,90000)==45000);fill_motion_provenance_cancel(&p,true);assert(p.lifecycle==4);
 uint64_t bad[]={0,UINT64_MAX};for(unsigned i=0;i<2;i++){fill_motion_provenance_start(&p,bad[i],2,1,0);fill_motion_provenance_complete(&p);assert(p.lifecycle==0&&fill_motion_provenance_remaining(&p,90000)==45000);}
 uint32_t epochs[]={0,UINT32_MAX};for(unsigned i=0;i<2;i++){fill_motion_provenance_start(&p,10000,epochs[i],1,0);fill_motion_provenance_complete(&p);assert(p.lifecycle==0);}
 fill_motion_provenance_start(&p,10001,2,1,100);fill_motion_provenance_complete(&p);assert(p.lifecycle==2&&fill_motion_provenance_remaining(&p,200)==44900); /* early legacy completion cannot imply45s */
 assert(fill_motion_provenance_remaining(&p,45100)==0);
 fill_motion_provenance_start(&p,10002,2,1,101);assert(fill_motion_provenance_remaining(&p,100)==45000);assert(fill_motion_provenance_remaining(&p,45100)==1);assert(fill_motion_provenance_remaining(&p,45101)==0);
 fill_motion_provenance_start(&p,10002,2,2,50000);assert(p.action==2&&p.lifecycle==1&&fill_motion_provenance_remaining(&p,50000)==45000);
 return 0;}
'''
  with tempfile.TemporaryDirectory()as tmp:
   d=Path(tmp);p=d/'test.c';p.write_text(code);exe=d/'test';subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'Core/Inc'),str(ROOT/'Core/Src/fill_motion_status.c'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 def test_exact_codec_every_byte_and_unaligned_output(self):
  code=r'''
#include "fill_motion_status.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(void){uint8_t request[32]={2,0},raw[58];for(unsigned i=0;i<8;i++){request[4+i]=(uint8_t)(0x11+i);request[24+i]=(uint8_t)(0x81+i);}memset(raw,0xEE,sizeof raw);
 FillMotionSnapshotV2 s={.mode=2,.active_action=0,.busy=0,.lifecycle=2,.accepted_action=1,.current_epoch=0x12345678U,.remaining_ms=0,.accepted_source=UINT64_C(0x0102030405060708),.minimum_remaining_ms=0,.origin_epoch=0x12345678U};
 fill_motion_status_v2_encode(raw+1,request,0,UINT64_C(0xA1A2A3A4A5A6A7A8),&s);if(raw[0]!=0xEE||raw[57]!=0xEE)return 1;for(unsigned i=1;i<=56;i++)printf("%02x",raw[i]);puts("");return 0;}
'''
  with tempfile.TemporaryDirectory()as tmp:
   d=Path(tmp);p=d/'codec.c';p.write_text(code);exe=d/'codec';subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'Core/Inc'),str(ROOT/'Core/Src/fill_motion_status.c'),str(p),'-o',str(exe)],check=True);actual=subprocess.check_output([str(exe)],text=True).strip()
   expected=(bytes([2,0,2,5])+bytes(range(0x11,0x19))+(0xA1A2A3A4A5A6A7A8).to_bytes(8,'little')+(0x12345678).to_bytes(4,'little')+bytes(range(0x81,0x89))+bytes([0,0,2,1])+bytes(4)+(0x0102030405060708).to_bytes(8,'little')+bytes(4)+(0x12345678).to_bytes(4,'little')).hex();self.assertEqual(actual,expected)
 def test_old_schema_definition_unchanged(self):
  new=json.loads((ROOT/'config/sedsnet.json').read_text());old=next(t for t in new['types']if t['name']=='AB_FILL_DRIVE_STATUS');motion=next(t for t in new['types']if t['name']=='AB_FILL_MOTION_STATUS_V2');self.assertEqual(old['element'],{'kind':'Static','data_type':'UInt8','count':40});self.assertEqual(motion['element'],{'kind':'Static','data_type':'UInt8','count':56})
