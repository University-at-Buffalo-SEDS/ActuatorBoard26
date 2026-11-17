"""Recovery freshness before network time, reboot mid-recovery, reset.

Freshness is SEDSnet's stamp window (seds_router_network_stamp_fresh): with no
network time, or a wrongly anchored one, nothing is fresh and nothing is
written; the Abort stays latched and Abort still gets through. SEDSnet 6
anchors a follower within one or two exchanges, after which recovery proceeds.
An interrupted NORMAL recovery completes after a reboot once network time is
back. A reset of a running board whose request went stale before its first
effect latches no Abort.

The actual recovery transport, storage, boot identity, floor and thread_comm C
run over the durable-recovery flash fixture; only the SDK clock is replaced.
"""
from pathlib import Path
import os,subprocess,tempfile,unittest
from native_sdk import configured_dependency_source
ROOT=Path(__file__).resolve().parents[1]
CLOCK_OLD='SedsResult seds_router_get_network_time_ms(SedsRouter*r,uint64_t*out){assert(sdk_unlocked());(void)r;*out=now;return SEDS_OK;}'
CLOCK_NEW=r'''static int net_mode; /* 0 synced (now), 1 unavailable, 2 valid but uptime-like */
SedsResult seds_router_get_network_time_ms(SedsRouter*r,uint64_t*out){assert(sdk_unlocked());(void)r;
 if(net_mode==1){return SEDS_IO;}
 *out=net_mode==2?5006972U:now;return SEDS_OK;}
#define STAMP_FIXTURE_READ (net_mode==1?UINT64_C(0):net_mode==2?UINT64_C(5006972):now)'''
MAIN=r'''
#define T0 UINT64_C(1794183589988)
static void normal_apply(void*context){(void)context;}
static void prove_command(uint64_t timestamp){
 thread_comm_msg_t msg={.cmd=12,.timestamp_ms=timestamp},received={0};uint32_t epoch=0;
 assert(thread_comm_admit_network(msg,timestamp)==TX_SUCCESS);
 assert(thread_comm_receive_owned(&received,&epoch)==TX_SUCCESS&&received.cmd==12);
 assert(thread_comm_manual_effect_owned(timestamp,epoch,normal_apply,NULL));
}
static uint32_t live_epoch(void){BoardRecoverySnapshot s={0};(void)board_recovery_snapshot(&s);assert(s.abort_epoch);return s.abort_epoch;}
static uint8_t recover(SedsRouter*r,uint64_t nonce,uint64_t query_time,uint64_t time,uint8_t desired){
 BoardRecoveryRequest q={.operation=BOARD_RECOVERY_QUERY,.nonce=nonce,.issuer_session=77};
 if(query_time){send_req(r,&q,query_time);assert(response[1]==0);}
 BoardRecoveryRequest rq={.operation=BOARD_RECOVERY_RECOVER,.desired_state=desired,.nonce=nonce+1,.issuer_session=77,.expected_boot_generation=board_recovery_boot_current(),.expected_abort_epoch=live_epoch()};
 unsigned before=responses;send_req(r,&rq,time);if(responses!=before)return response[1];
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
int main(int argc,char**argv){
 assert(argc==3);scenario=argv[1];image_path=argv[2];
 int cookie=0;SedsRouter*r=(SedsRouter*)&cookie;
 if(!strcmp(scenario,"reboot_unsynced")){
  net_mode=1;boot(true);assert(flight_state_cache_init(r)==SEDS_OK);board_recovery_transport_set_router(r);
  /* The interrupted NORMAL recovery from the previous boot generation. */
  assert(board_bootstrap_storage_pending()&&thread_comm_get_abort()&&!persistent_store_controls_ready());
  uint64_t gen=board_recovery_boot_current();assert(gen==43);
  /* No network time yet after the reboot: not fresh, still pending. */
  assert(recover(r,31,T0+60001,T0+60002,0)==BOARD_RECOVERY_RESULT_STALE);
  assert(board_bootstrap_storage_pending()&&thread_comm_get_abort());
  /* Network time is back: the station's next attempt completes. */
  net_mode=0;now=T0+60003;
  assert(recover(r,33,T0+60003,T0+60004,0)==BOARD_RECOVERY_RESULT_OK);
  assert(!thread_comm_get_abort()&&!board_abort_latch_is_set()&&persistent_store_controls_ready()&&!board_bootstrap_storage_pending());
  prove_command(T0+60005);puts("interrupted NORMAL recovery completes after reboot once network time is back PASS");return 0;
 }
 boot(false);assert(flight_state_cache_init(r)==SEDS_OK);board_recovery_transport_set_router(r);
 assert(!thread_comm_get_abort()&&persistent_store_controls_ready());
 if(!strncmp(scenario,"unsynced",8)){
  net_mode=!strcmp(scenario,"unsynced_uptime")?2:1;
  assert(thread_comm_request_manual_abort()==TX_SUCCESS&&thread_comm_get_abort());
  unsigned writes=media_writes;
  /* No usable network time: no request is fresh, nothing is written, the
   * QUERY is answered but never arms a recovery. */
  BoardRecoveryRequest early={.operation=BOARD_RECOVERY_RECOVER,.desired_state=0,.nonce=2,.issuer_session=77,.expected_boot_generation=board_recovery_boot_current(),.expected_abort_epoch=live_epoch()};
  send_req(r,&early,T0);assert(response[1]==BOARD_RECOVERY_RESULT_STALE&&media_writes==writes&&thread_comm_get_abort());
  assert(recover(r,3,T0+1,T0+2,0)==BOARD_RECOVERY_RESULT_STALE&&media_writes==writes&&thread_comm_get_abort());
  /* Abort still gets through. */
  assert(thread_comm_request_manual_abort()==TX_SUCCESS&&thread_comm_get_abort());
  /* SEDSnet anchored the board: a fresh station request completes. */
  net_mode=0;now=T0+50000;
  assert(recover(r,11,T0+50000,T0+50001,0)==BOARD_RECOVERY_RESULT_OK);
  assert(!thread_comm_get_abort()&&!board_abort_latch_is_set()&&persistent_store_controls_ready());
  /* The replay floor is active; ordinary commands use the same window. */
  uint32_t endpoint=SEDS_EP_ACTUATOR_BOARD;uint8_t cmd=12;now=T0+50002;
  SedsPacketView packet={.ty=SEDS_DT_ACTUATOR_COMMAND,.sender="GS",.sender_len=2,.endpoints=&endpoint,.num_endpoints=1,.payload=&cmd,.payload_len=1,.timestamp=T0+50002};
  assert(board_recovery_floor_active()&&!board_recovery_command_is_stale(&packet));
  packet.timestamp=T0+50002+20000;assert(board_recovery_command_is_stale(&packet));
  now+=20000;packet.timestamp=T0+50002;assert(board_recovery_command_is_stale(&packet));
  /* Below the floor (the Reset's stamp): refused however fresh it looks. */
  now=T0+50001;packet.timestamp=T0+50001;assert(board_recovery_command_is_stale(&packet));
  puts("recovery and command freshness by the SEDSnet stamp window PASS");return 0;
 }
 if(!strcmp(scenario,"interrupt")){
  net_mode=0;now=T0-1;assert(thread_comm_request_manual_abort()==TX_SUCCESS);
  BoardRecoveryRequest q={.operation=BOARD_RECOVERY_QUERY,.nonce=21,.issuer_session=77};send_req(r,&q,T0-1);assert(response[1]==0);
  BoardRecoveryRequest rq={.operation=BOARD_RECOVERY_RECOVER,.desired_state=0,.nonce=22,.issuer_session=77,.expected_boot_generation=board_recovery_boot_current(),.expected_abort_epoch=live_epoch()};
  scenario="floor_after";begin_phase(3);send_req(r,&rq,T0);main_service();
  assert(!"the fixture power cut did not happen");
 }
 if(!strcmp(scenario,"reset_stale")||!strcmp(scenario,"reset_late")){
  /* A running board told to reset: the request went stale before the main
   * service reached it (stale), or after the board had stopped (late). */
  assert(thread_comm_set_flight_state(1)==TX_SUCCESS);now=T0;
  BoardRecoveryRequest rq={.operation=BOARD_RECOVERY_RECOVER,.desired_state=0,.nonce=41,.issuer_session=77,.expected_boot_generation=board_recovery_boot_current(),.expected_abort_epoch=live_epoch()};
  unsigned before=responses,writes=media_writes;send_req(r,&rq,T0);assert(responses==before);
  if(!strcmp(scenario,"reset_stale")){
   now+=16000;ticks+=16000;main_service();
   assert(response[1]==BOARD_RECOVERY_RESULT_STALE&&media_writes==writes);
   assert(!thread_comm_get_abort()&&!board_abort_latch_is_set()&&persistent_store_controls_ready());
   prove_command(T0+16001);
   assert(recover(r,43,T0+16002,T0+16003,0)==BOARD_RECOVERY_RESULT_OK&&!thread_comm_get_abort());
   puts("stale reset of a running board latches no Abort and keeps commands PASS");return 0;
  }
  main_owner=true;board_bootstrap_storage_service();main_owner=false;assert(response[1]==BOARD_RECOVERY_RESULT_PENDING);
  now+=16000;ticks+=16000;main_service();
  assert(response[1]==BOARD_RECOVERY_RESULT_OK&&!thread_comm_get_abort()&&!board_abort_latch_is_set()&&persistent_store_controls_ready());
  puts("reset that already stopped the board finishes to its safe target PASS");return 0;
 }
 return 2;
}
'''
class UnsyncedRecovery(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='unsynced-recovery-');p=Path(cls.tmp.name)
  sdk=Path(os.environ['SEDSNET_TEST_SOURCE']);lc=configured_dependency_source(ROOT,'sedslaunchcore','SDK_FREE_LAUNCHCORE_SOURCE')
  fixture=ROOT/'tests/fixtures/durable_recovery'
  harness=(fixture/'harness.c').read_text().split('int main(int argc,char**argv){',1)[0]
  assert CLOCK_OLD in harness
  (p/'unsynced.c').write_text(harness.replace(CLOCK_OLD,CLOCK_NEW)+MAIN)
  modules=['board_abort_latch.c','thread_comm.c','board_recovery_wire.c','board_recovery_boot.c','board_recovery_transport.c','flight_state_cache.c','persistent_store.c','board_bootstrap_storage.c','bootstrap_marker.c','fill_settings.c','fill_motion_status.c']
  cls.exe=p/'unsynced'
  subprocess.run(['cc','-DEXPECTED_CAP=32','-DTELEMETRY_ENABLED=1','-DSAFE_MASK=0','-DRECOVERY_ENDPOINT=2000','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-attributes','-fsanitize=address,undefined','-I'+str(fixture),'-I'+str(ROOT/'Core/Inc'),'-I'+str(ROOT/'Core/Src'),'-I'+str(sdk/'C-Headers'),'-I'+str(lc/'bootloader/include'),*[str(ROOT/'Core/Src'/n)for n in modules],str(ROOT/'Bootloader/storage_dispatch.c'),str(lc/'bootloader/src/persist.c'),str(lc/'bootloader/src/crc32.c'),str(p/'unsynced.c'),'-o',str(cls.exe)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def run_case(self,name,image,code=0):
  out=subprocess.run([str(self.exe),name,str(image)],capture_output=True,text=True,timeout=60)
  self.assertEqual(out.returncode,code,out.stdout+out.stderr)
  return out
 def test_recovery_waits_for_network_time(self):
  self.run_case('unsynced',Path(self.tmp.name)/'a.bin')
 def test_recovery_waits_for_a_correctly_anchored_clock(self):
  self.run_case('unsynced_uptime',Path(self.tmp.name)/'b.bin')
 def test_interrupted_normal_recovery_completes_after_reboot(self):
  image=Path(self.tmp.name)/'c.bin'
  self.run_case('interrupt',image,42)
  self.run_case('reboot_unsynced',image)
 def test_stale_reset_of_running_board_latches_no_abort(self):
  self.run_case('reset_stale',Path(self.tmp.name)/'d.bin')
 def test_reset_that_stopped_the_board_finishes_after_window(self):
  self.run_case('reset_late',Path(self.tmp.name)/'e.bin')
if __name__=='__main__':unittest.main()
