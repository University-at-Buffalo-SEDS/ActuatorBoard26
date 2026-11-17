"""Station requests across GS network time steps (SEDSnet 6 epochs).

A GS time step in either direction starts a newer epoch. The board orders
every stamp by (epoch, ms) and asks SEDSnet whether a stamp is inside its
command window, translated across the steps it remembers, so:
- after a 4 h step back a fresh command is admitted at once, while a copy
  stamped before the step is judged by its real age (a stale one is refused)
  and a replay below the replay floor stays refused;
- a recovery Reset after the step is admitted although its ms are hours below
  the last Reset's, and a replay of the pre-Reset request is refused;
- a 3.8 h step forward behaves the same way;
- a request stamped in a newer epoch the board has not adopted yet is fresh
  only while the board re-anchors (at most the window), as SEDSnet decides;
- the floor's epoch survives a reboot (the router epoch is raised to it);
- a manual Abort stamped after a step back is never taken for one older than
  the last Reset.

The actual recovery transport, storage, floor, bootstrap and thread_comm C run
over the durable-recovery flash fixture; the SDK clock is the SEDSnet
stamp-window fixture (tests/fixtures/network_stamp_fixture.h).
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from native_sdk import configured_dependency_source

ROOT = Path(__file__).resolve().parents[1]
MAIN = r'''
#include "board_stamp.h"
#define T0 UINT64_C(1794183589988)
#define HOURS_4 UINT64_C(14400000)
#define HOURS_3_8 UINT64_C(13680000)
static void normal_apply(void*context){(void)context;}
/* A command stamped (epoch, ms) passes the transport gate and thread_comm. */
static bool command_admitted(SedsRouter*r,uint16_t epoch,uint64_t ms){
 (void)r;uint32_t endpoint=SEDS_EP_ACTUATOR_BOARD;uint8_t cmd=12;
 SedsPacketView p={.ty=SEDS_DT_ACTUATOR_COMMAND,.sender="GS",.sender_len=2,.endpoints=&endpoint,.num_endpoints=1,.payload=&cmd,.payload_len=1,.timestamp=ms,.time_epoch=epoch};
 if(board_recovery_sync_command_cutoff()!=SEDS_OK||board_recovery_command_is_stale(&p))return false;
 const uint64_t stamp=board_stamp_of(&p);thread_comm_msg_t msg={.cmd=12,.timestamp_ms=stamp},received={0};uint32_t owner=0;
 if(thread_comm_admit_network(msg,stamp)!=TX_SUCCESS)return false;
 assert(thread_comm_receive_owned(&received,&owner)==TX_SUCCESS&&received.cmd==12&&received.timestamp_ms==stamp);
 assert(thread_comm_manual_effect_owned(stamp,owner,normal_apply,NULL));return true;
}
static uint32_t live_epoch(void){BoardRecoverySnapshot s={0};(void)board_recovery_snapshot(&s);assert(s.abort_epoch);return s.abort_epoch;}
/* Abort, then QUERY + RECOVER (Reset to Startup), both stamped in `epoch`. */
static uint8_t reset(SedsRouter*r,uint16_t epoch,uint64_t nonce,uint64_t query_ms,uint64_t ms){
 assert(thread_comm_request_manual_abort()==TX_SUCCESS&&thread_comm_get_abort());
 request_epoch=epoch;BoardRecoveryRequest q={.operation=BOARD_RECOVERY_QUERY,.nonce=nonce,.issuer_session=77};
 send_req(r,&q,query_ms);assert(response[1]==0);
 BoardRecoveryRequest rq={.operation=BOARD_RECOVERY_RECOVER,.desired_state=0,.nonce=nonce+1,.issuer_session=77,.expected_boot_generation=board_recovery_boot_current(),.expected_abort_epoch=live_epoch()};
 unsigned before=responses;send_req(r,&rq,ms);request_epoch=0;if(responses!=before)return response[1];
 main_service();return response[1];
}
static void boot(bool load){
 if(load){FILE*f=fopen(image_path,"rb");assert(f&&fread(&image,sizeof image,1,f)==1&&fclose(f)==0);}
 else{memset(image.nvm,255,NVM_SIZE);launchcore_storage_set_driver(&launchcore_board_storage_driver);seed_control("seed_healthy");}
 launchcore_storage_set_driver(&launchcore_board_storage_driver);fixture=false;
 board_abort_latch_init();assert(persistent_store_boot_begin()==LAUNCHCORE_PERSIST_OK);assert(board_recovery_boot_generation());board_bootstrap_storage_probe_boot();fill_settings_restore();
 assert(persistent_store_runtime_start()==TX_SUCCESS);thread_present=true;TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==TX_SUCCESS);
 board_recovery_bind_owner(read_outputs,prepare_outputs,finish_outputs);assert(board_bootstrap_storage_bind_owner(stop_owner));
}
static void save(void){FILE*f=fopen(image_path,"wb");assert(f&&fwrite(&image,sizeof image,1,f)==1&&fclose(f)==0);}
int main(int argc,char**argv){
 assert(argc==3);scenario=argv[1];image_path=argv[2];
 int cookie=0;SedsRouter*r=(SedsRouter*)&cookie;
 if(!strcmp(scenario,"reboot_floor_epoch")){
  boot(true);assert(flight_state_cache_init(r)==SEDS_OK);board_recovery_transport_set_router(r);
  /* The floor persisted in epoch 1 reads back with its epoch: the router's
   * epoch is raised to it while the board has no network time. */
  uint64_t floor=0;assert(board_recovery_floor_read_cutoff(&floor)&&board_stamp_epoch(floor)==1U);
  assert(board_recovery_floor_cached_cutoff(&floor)&&board_stamp_epoch(floor)==1U);
  /* The network is in epoch 1 again: a pre-Reset copy stays refused, a
   * fresh request is admitted. */
  stamp_fixture_epoch=1U;now=board_stamp_ms(floor)+5000U;
  assert(!command_admitted(r,1,board_stamp_ms(floor)));assert(!command_admitted(r,0,now));
  assert(command_admitted(r,1,now));
  puts("persisted floor keeps its epoch across reboot PASS");return 0;
 }
 boot(false);assert(flight_state_cache_init(r)==SEDS_OK);board_recovery_transport_set_router(r);
 assert(!thread_comm_get_abort()&&persistent_store_controls_ready());
 /* A Reset before the step: the floor is (0, T0+1). */
 now=T0+1;assert(reset(r,0,11,T0,T0+1)==BOARD_RECOVERY_RESULT_OK&&!thread_comm_get_abort());
 assert(board_recovery_floor_active());now=T0+2;assert(command_admitted(r,0,T0+2));
 if(!strcmp(scenario,"backward")||!strcmp(scenario,"forward")||!strcmp(scenario,"reboot_floor_epoch_seed")){
  const bool back=strcmp(scenario,"forward")!=0;
  /* One copy sent 1 s before the step, one 20 s before it. */
  now=T0+30000;const uint64_t recent=now-1000U,old=now-20000U;
  stamp_fixture_step(back?now-HOURS_4:now+HOURS_3_8);assert(stamp_fixture_epoch==1U);
  now+=500U;
  /* A fresh command on the new time base is admitted at once. */
  assert(command_admitted(r,1,now));
  /* The copies stamped before the step are judged by their real age, never
   * by comparing old-base ms with the new clock: 1.5 s old is fresh, 20.5 s
   * old is refused; ordering keeps both behind the newer command. */
  {uint32_t endpoint=SEDS_EP_ACTUATOR_BOARD;uint8_t cmd=12;
   SedsPacketView p={.ty=SEDS_DT_ACTUATOR_COMMAND,.sender="GS",.sender_len=2,.endpoints=&endpoint,.num_endpoints=1,.payload=&cmd,.payload_len=1,.timestamp=recent,.time_epoch=0};
   assert(!board_recovery_command_is_stale(&p));p.timestamp=old;assert(board_recovery_command_is_stale(&p));}
  /* A replay at or below the floor stays refused. */
  assert(!command_admitted(r,0,T0+1));assert(!command_admitted(r,0,T0));
  /* A Reset after the step: its ms are hours below (or above) the last
   * Reset's, its epoch is newer, so it is admitted. */
  assert(reset(r,1,21,now+1,now+2)==BOARD_RECOVERY_RESULT_OK&&!thread_comm_get_abort());
  uint64_t floor=0;assert(board_recovery_floor_read_cutoff(&floor)&&floor==board_stamp_pack(1,now+2));
  /* A replay of the pre-step Reset request is refused. */
  assert(thread_comm_request_manual_abort()==TX_SUCCESS);
  request_epoch=0;BoardRecoveryRequest rq={.operation=BOARD_RECOVERY_RECOVER,.desired_state=0,.nonce=31,.issuer_session=77,.expected_boot_generation=board_recovery_boot_current(),.expected_abort_epoch=live_epoch()};
  unsigned writes=media_writes;send_req(r,&rq,T0+1);main_service();assert(response[1]==BOARD_RECOVERY_RESULT_STALE&&media_writes==writes&&thread_comm_get_abort());
  /* A manual Abort stamped after the step is not older than the floor; one
   * stamped before the last Reset is retired. */
  assert(reset(r,1,41,now+3,now+4)==BOARD_RECOVERY_RESULT_OK&&!thread_comm_get_abort());
  assert(thread_comm_request_manual_abort_original_utc(board_stamp_pack(0,T0+30000))==TX_SUCCESS&&!thread_comm_get_abort());
  assert(thread_comm_request_manual_abort_original_utc(board_stamp_pack(1,now+5))==TX_SUCCESS&&thread_comm_get_abort());
  if(!strcmp(scenario,"reboot_floor_epoch_seed")){
   assert(reset(r,1,51,now+6,now+7)==BOARD_RECOVERY_RESULT_OK);save();
  }
  printf("%s step: fresh command, real-age copies, floor replay, Reset and Abort PASS\n",back?"backward 4 h":"forward 3.8 h");return 0;
 }
 if(!strcmp(scenario,"reanchoring")){
  /* The GS stepped back into epoch 1; this board heard of it 2 s ago and is
   * still re-anchoring (its clock is on epoch 0). */
  now=T0+30000;stamp_fixture_hear_newer(1,2000);
  const uint64_t gs=now-HOURS_4;
  assert(command_admitted(r,1,gs));
  assert(reset(r,1,61,gs+1,gs+2)==BOARD_RECOVERY_RESULT_OK&&!thread_comm_get_abort());
  /* Known for longer than the window without adopting it: not fresh. */
  stamp_fixture_hear_newer(2,16000);assert(!command_admitted(r,2,gs+10));
  /* Never heard of before this request: it starts the re-anchoring. */
  assert(command_admitted(r,3,gs+11));
  puts("newer-epoch requests while re-anchoring PASS");return 0;
 }
 return 2;
}
'''


class NetworkTimeEpochs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='network-time-epochs-')
        p = Path(cls.tmp.name)
        sdk = Path(os.environ['SEDSNET_TEST_SOURCE'])
        lc = configured_dependency_source(ROOT, 'sedslaunchcore', 'SDK_FREE_LAUNCHCORE_SOURCE')
        fixture = ROOT / 'tests/fixtures/durable_recovery'
        harness = (fixture / 'harness.c').read_text().split('int main(int argc,char**argv){', 1)[0]
        (p / 'epochs.c').write_text(harness + MAIN)
        modules = ['board_abort_latch.c', 'thread_comm.c', 'board_recovery_wire.c', 'board_recovery_boot.c',
                   'board_recovery_transport.c', 'flight_state_cache.c',
                   'persistent_store.c', 'board_bootstrap_storage.c', 'bootstrap_marker.c',
                   'fill_settings.c', 'fill_motion_status.c']
        cls.exe = p / 'epochs'
        subprocess.run(['cc', '-DEXPECTED_CAP=32', '-DTELEMETRY_ENABLED=1', '-DSAFE_MASK=0', '-DRECOVERY_ENDPOINT=2000',
                        '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-attributes',
                        '-fsanitize=address,undefined', '-I' + str(fixture), '-I' + str(ROOT / 'Core/Inc'),
                        '-I' + str(ROOT / 'Core/Src'), '-I' + str(sdk / 'C-Headers'), '-I' + str(lc / 'bootloader/include'),
                        *[str(ROOT / 'Core/Src' / n) for n in modules], str(ROOT / 'Bootloader/storage_dispatch.c'),
                        str(lc / 'bootloader/src/persist.c'), str(lc / 'bootloader/src/crc32.c'), str(p / 'epochs.c'),
                        '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, name, image):
        out = subprocess.run([str(self.exe), name, str(image)], capture_output=True, text=True, timeout=60)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_backward_4h_step(self):
        self.run_case('backward', Path(self.tmp.name) / 'a.bin')

    def test_forward_3_8h_step(self):
        self.run_case('forward', Path(self.tmp.name) / 'b.bin')

    def test_newer_epoch_while_reanchoring(self):
        self.run_case('reanchoring', Path(self.tmp.name) / 'c.bin')

    def test_floor_epoch_survives_reboot(self):
        image = Path(self.tmp.name) / 'd.bin'
        self.run_case('reboot_floor_epoch_seed', image)
        self.run_case('reboot_floor_epoch', image)

    def test_packed_stamp_order_is_seds_stamp_cmp(self):
        code = r'''
#include <assert.h>
#include "sedsnet.h"
#include "board_stamp.h"
int main(void){
 const uint16_t epochs[]={0,1,2,0x7FFF,0xFFFE};
 const uint64_t ms[]={1,999,UINT64_C(1794183589988),UINT64_C(1794183589988)-UINT64_C(14400000),BOARD_STAMP_MS_MASK-1U};
 for(unsigned a=0;a<5*5;a++)for(unsigned b=0;b<5*5;b++){
  const uint16_t ea=epochs[a/5],eb=epochs[b/5];const uint64_t ma=ms[a%5],mb=ms[b%5];
  const uint64_t pa=board_stamp_pack(ea,ma),pb=board_stamp_pack(eb,mb);
  assert(board_stamp_epoch(pa)==ea&&board_stamp_ms(pa)==ma);
  const int cmp=seds_stamp_cmp(ea,ma,eb,mb);assert((pa<pb)==(cmp<0)&&(pa==pb)==(cmp==0));
 }
 assert(board_stamp_pack(3,0)==0U&&board_stamp_pack(3,UINT64_MAX)==UINT64_MAX&&board_stamp_pack(0,BOARD_STAMP_MS_MASK+1U)==UINT64_MAX);
 return 0;}
'''
        sdk = Path(os.environ['SEDSNET_TEST_SOURCE'])
        exe = Path(self.tmp.name) / 'order'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-I' + str(sdk / 'C-Headers'), '-I' + str(ROOT / 'Core/Inc'), '-x', 'c', '-', '-o', str(exe)],
                       input=code, text=True, check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
