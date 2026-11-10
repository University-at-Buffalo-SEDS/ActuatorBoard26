"""Actual owner/queue/callback source; bounded host-only MMIO/ThreadX fixtures.

The selected source may be canonical for a preserved causal old RED. No SDK,
network, physical outputs or private dependency override is involved.
"""
from pathlib import Path
import ast,os,re,subprocess,tempfile,unittest
from test_launch_abort_policy import function
from board_identity import BOARD_REPOSITORY
BASE=Path(__file__).resolve().parents[1]
ROOT=Path(os.environ.get('BOARD_POSTLAUNCH_SOURCE',BASE))
def literal(path,name):
 tree=ast.parse(path.read_text())
 return next(n.value.value for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name)and t.id==name for t in n.targets))
def prefix():
 harness=literal(BASE/'tests/test_verified_launch_latch.py','HARNESS').split('static UINT launch_request(void)')[0]
 harness=harness.replace('bool persistent_store_controls_ready(void){return true;}','static bool storage_ready=true;bool persistent_store_controls_ready(void){return storage_ready;}')
 harness=harness.replace('bool persistent_store_recovery_ready(void){return true;}','bool persistent_store_recovery_ready(void){return storage_ready;}')
 return harness.replace('ACTUAL_OWNER','')+'\nstatic uint32_t sampled_launch_tick(void){return 0U;}\nUINT fixture_timed_launch(thread_comm_msg_t original,const char*sender,uint32_t length){return thread_comm_admit_network_launch_original_at(original,sender,length,10000U,0U,sampled_launch_tick);}\n'
def failure_prefix():
 pre=prefix()
 old='UINT tx_mutex_put(TX_MUTEX*m){assert(m->locked);m->locked=0;return 0;}'
 assert old in pre
 return pre.replace(old,'static bool fail_next_state_release;UINT tx_mutex_put(TX_MUTEX*m){assert(m->locked);m->locked=0;if(m==state_mutex&&fail_next_state_release){fail_next_state_release=false;return TX_MUTEX_ERROR;}return 0;}')
def compile_case(code,case,inline_state=False):
 # Calls in the fixture offer an explicit coherent pre-admission clock.
 # The production source is separately compiled/included under its real names.
 code=code.replace('thread_comm_admit_network_launch_original(', 'fixture_timed_launch(')
 effective=re.sub(r'#ifdef VB\n.*?#endif','',code,flags=re.S) if BOARD_REPOSITORY=='ActuatorBoard26' else code
 if 'ULONG tx_time_get(void){' not in effective:code+='\nULONG tx_time_get(void){return 0;}\n'
 if 'SedsResult Valve_Command_handler(' in code:
  telemetry=(ROOT/'Core/Src/telemetry.c').read_text()
  adapter='\nstatic SedsResult seds_router_get_network_time_ms(void*r,uint64_t*out){(void)r;*out=10000U;return SEDS_OK;}\n'
  if 'g_router' not in code:adapter='static struct{void*r;}g_router={(void*)1};\n'+adapter
  helpers=adapter+function(telemetry,'launch_monotonic_tick')+'\n'+function(telemetry,'admit_timed_launch')+'\n'
  code=code.replace('SedsResult Valve_Command_handler(',helpers+'SedsResult Valve_Command_handler(')
  code=code.replace('static struct{void*r;}g_router;','static struct{void*r;}g_router={(void*)1};')
 with tempfile.TemporaryDirectory(prefix='postlaunch-storage-')as temp:
  p=Path(temp);(p/'main.h').write_text(literal(BASE/'tests/test_recovery_durable_barrier.py','MAIN'))
  tree=ast.parse((BASE/'tests/test_board_recovery_owner.py').read_text())
  tx=next(n.args[0].value for n in ast.walk(tree)if isinstance(n,ast.Call)and isinstance(n.func,ast.Attribute)and n.func.attr=='write_text'and n.args and isinstance(n.args[0],ast.Constant)and isinstance(n.args[0].value,str)and 'typedef struct{UINT locked;}TX_MUTEX'in n.args[0].value)
  (p/'tx_api.h').write_text(tx);(p/'case.c').write_text(code);exe=p/'case'
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(ROOT/'Core/Inc'),str(ROOT/'Core/Src/board_abort_latch.c'),*([]if inline_state else[str(ROOT/'Core/Src/thread_comm.c')]),str(p/'case.c'),'-o',str(exe)],check=True,timeout=60)
  result=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=5)
  assert result.returncode==0,result.stdout+result.stderr
  return result
class PostlaunchStorageContinuityTests(unittest.TestCase):
 def test_actual_callback_full_queue_refusal_retry_must_really_commit_original_once(self):
  telemetry=(ROOT/'Core/Src/telemetry.c').read_text()
  try:replay=function(telemetry,'is_replayed_igniter_sequence_command')
  except AssertionError:replay=''
  code=prefix()+r'''
typedef int SedsResult;
#define SEDS_OK 0
#define SEDS_ERR -1
#define SEDS_BAD_ARG -2
#define SEDS_IO -3
#define SEDS_HANDLER_ERROR -4
typedef struct{uint32_t ty;const char*sender;size_t sender_len;uint64_t timestamp;const uint8_t*payload;size_t payload_len;}SedsPacketView;
static unsigned g_command_queue_drop_count;
static int board_recovery_sync_command_cutoff(void){return SEDS_OK;}
static bool board_recovery_command_is_stale(const SedsPacketView*p){(void)p;return false;}
static int32_t seds_pkt_get_u8(const SedsPacketView*p,uint8_t*out,size_t count){assert(count==1U);*out=p->payload[0];return 1;}
'''+replay+'\n'+function(telemetry,'Valve_Command_handler')+r'''
int main(int argc,char**argv){
 (void)argv;assert(argc==2);board_abort_latch_init();TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==TX_SUCCESS);
 thread_comm_msg_t filler={.cmd=CMD_IGNITER_OFF,.timestamp_ms=100};
 for(unsigned i=0;i<THREAD_COMM_QUEUE_DEPTH;i++)assert(thread_comm_send(filler,TX_NO_WAIT)==TX_SUCCESS);
 const uint8_t command=CMD_IGNITER_SEQUENCE;
 const SedsPacketView original={.sender="GS",.sender_len=2,.timestamp=10000,.payload=&command,.payload_len=1};
 assert(Valve_Command_handler(&original,0)==SEDS_ERR);
 thread_comm_msg_t message;assert(thread_comm_receive(&message,TX_NO_WAIT)==TX_SUCCESS);
 assert(Valve_Command_handler(&original,0)==SEDS_OK);
 unsigned launches=0;uint32_t epoch=0;
 while(thread_comm_receive_owned(&message,&epoch)==TX_SUCCESS)if(message.cmd==CMD_IGNITER_SEQUENCE)launches++;
 fprintf(stderr,"ACTUAL_CALLBACK fullqueue_refused_then_same_original_retry_launch_entries=%u\n",launches);
 assert(launches==1U&&"replayed OK must own the actual original Launch queue entry");return 0;
}
'''
  compile_case(code,'fullqueue-retry')

 def test_actual_main_loop_keeps_original_pending_and_running_launch_on_storage_failure(self):
  main=(ROOT/'Core/Src/main_task.c').read_text()
  entry=function(main,'main_task_entry');loop=entry[entry.index('    for (;;)'):entry.rfind('}')]
  context=re.search(r'typedef struct\{[^\n]*\} LaunchStart;',main).group(0)
  code=failure_prefix()+r"""
#include <setjmp.h>
#include "fill_owner_gate.h"
#include "board_recovery.h"
static jmp_buf done;
static ULONG ticks;
static unsigned iterations,physical_on,physical_off;
static uint8_t g_aborted,g_igniter_on,g_nitrogen_open,g_nitrous_open,g_plumbing_retract_active;
static uint8_t g_launch_sequence_active,g_launch_sequence_igniter_started,g_launch_sequence_holdoff_active;
static bool g_fill_mode_status_pending;
static uint32_t g_launch_manual_epoch,g_plumbing_retract_steps_remaining,g_main_task_loop_count;
static thread_comm_launch_owner_t g_launch_owner;
static ULONG g_launch_sequence_igniter_start_ticks,g_launch_sequence_igniter_stop_ticks,g_launch_sequence_holdoff_start_ticks;
#define TX_TIMER_TICKS_PER_SECOND 1000U
#define LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS 5000U
#define LAUNCH_SEQUENCE_IGNITER_LATENCY_COMPENSATION_MS 750U
#define LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS 10000U
#define LAUNCH_SEQUENCE_RESTART_HOLDOFF_MS 1500U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_AGE_MS 5000U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_FUTURE_MS 1000U
#define BOARD_WATCHDOG_CONTROL 0
#define GPIO_PIN_RESET 0
#define IGNITER_PIN_GPIO_Port 0
#define IGNITER_PIN_Pin 0
#define NITROGEN_PIN_GPIO_Port 0
#define NITROGEN_PIN_Pin 0
#define N2_SIG_GPIO_Port 0
#define N2_SIG_Pin 0
#define NITROUS_PIN_GPIO_Port 0
#define NITROUS_PIN_Pin 0
#define N20_SIG_GPIO_Port 0
#define N20_SIG_Pin 0
#define BACKUP_VALVE_EN_GPIO_Port 0
#define BACKUP_VALVE_EN_Pin 0
static int stepper;
static void HAL_GPIO_WritePin(int port,int pin,int value){(void)port;(void)pin;(void)value;}
static void stepperSleep(int*p){(void)p;}
static void stepperStopAsleepImmediate(int*p){(void)p;}
static void all_fill_outputs_off(void){}
static void fill_stop_locked(bool blocked,void*p){(void)blocked;(void)p;}
static uint8_t igniter_on(void){physical_on++;g_igniter_on=1;return 1;}
static void igniter_off(void){physical_off++;g_igniter_on=0;}
static void publish_expected_outputs(void){}
static int publish_umbilical_status(unsigned command,unsigned state){(void)command;(void)state;return 0;}
ULONG tx_time_get(void){return ticks;}
static void board_watchdog_progress(int tag){(void)tag;iterations++;assert(iterations<50);}
static void board_bootstrap_storage_service(void){}
static void fill_settings_service(void){}
static void sample_main_task_stack(void){}
static void service_plumbing_retract(void){}
static void service_auxiliary_retract(void){}
static void publish_umbilical_statuses_if_due(ULONG*p){(void)p;}
static void retry_pending_status_reports(void){}
"""+context+'\n'+'\n'.join(function(main,n)for n in ['ms_to_ticks','start_launch_sequence_locked','start_launch_sequence','service_launch_sequence_locked','service_launch_sequence','manual_stop_apply_locked','main_task_force_outputs_safe_off'])+r"""
/* No other command is inserted; this adapter routes the actual dequeued Launch
 * into its real owner. The complete real main-loop body follows unchanged. */
static void handle_command(thread_comm_msg_t message,uint32_t epoch){assert(message.cmd==CMD_IGNITER_SEQUENCE);start_launch_sequence(message.timestamp_ms,epoch);}
static unsigned scenario;static bool duplicate_running;
void tx_thread_sleep(ULONG delay){
 (void)delay;
 if(iterations==1){assert(g_launch_sequence_active&&!thread_comm_get_abort());if(duplicate_running){thread_comm_msg_t original={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=10000};fail_next_state_release=true;assert(thread_comm_admit_network_launch_original(original,"GS",2)==THREAD_COMM_COMMIT_UNCERTAIN);assert(thread_comm_launch_continuity());}storage_ready=false;test_tamp.BKP28R=test_tamp.BKP29R=0;drop_launch=1;}
 ticks+=1000;
 if(scenario==1&&iterations==6){assert(g_igniter_on&&physical_on==1);assert(thread_comm_request_manual_abort()==TX_SUCCESS);assert(!g_igniter_on&&!thread_comm_launch_continuity());longjmp(done,1);}
 if(iterations==7){assert(g_igniter_on&&physical_on==1&&!thread_comm_get_abort());longjmp(done,1);}
}
static void actual_control_loop(void){thread_comm_msg_t msg;uint32_t command_epoch=0;ULONG last_umbilical_status_ticks=0;
"""+loop+r"""
}
int main(int argc,char**argv){
 assert(argc==2);scenario=!strcmp(argv[1],"manual-stop")||!strcmp(argv[1],"duplicate-running-stop");duplicate_running=!strncmp(argv[1],"duplicate-running",17);board_abort_latch_init();TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==0);
 assert(thread_comm_bind_manual_stop_apply(manual_stop_apply_locked)==0);
 thread_comm_msg_t request={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=10000};assert(thread_comm_admit_network_launch_original(request,"GS",2)==TX_SUCCESS);
 if(!strcmp(argv[1],"duplicate-pending")){fail_next_state_release=true;assert(thread_comm_admit_network_launch_original(request,"GS",2)==THREAD_COMM_COMMIT_UNCERTAIN);assert(thread_comm_launch_continuity());}
 /* Failure before owner dequeue, plus SECOND backup loss, cannot cancel the
  * committed exact original. There is no storage-ready override. */
 if(strcmp(argv[1],"running")&&!duplicate_running){storage_ready=false;test_tamp.BKP28R=test_tamp.BKP29R=0;drop_launch=1;}
 if(setjmp(done)==0)actual_control_loop();
 assert(!persistent_store_controls_ready());return 0;
}
"""
  if 'thread_comm_launch_owner_t' not in (ROOT/'Core/Inc/thread_comm.h').read_text():
   code=code.replace('static thread_comm_launch_owner_t g_launch_owner;','').replace('&&!thread_comm_launch_continuity()','')
  compile_case(code,'pending-running')
  compile_case(code,'running')
  compile_case(code,'manual-stop')
  compile_case(code,'duplicate-pending')
  compile_case(code,'duplicate-running')
  compile_case(code,'duplicate-running-stop')

 def test_actual_receipt_initial_failure_identity_consumption_and_manual_stop(self):
  code=prefix()+r"""
static unsigned effects;
static void effect(void*context){(void)context;effects++;}
int main(int argc,char**argv){
 assert(argc==2);board_abort_latch_init();TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==TX_SUCCESS);
 thread_comm_launch_owner_t owner={0};
"""
  code+=r"""
 thread_comm_msg_t message={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=10000};
 if(!strcmp(argv[1],"initial-failure")){drop_launch=1;assert(thread_comm_admit_network_launch_original(message,"GS",2)!=TX_SUCCESS);assert(!thread_comm_launch_continuity());assert(thread_comm_receive(&message,TX_NO_WAIT)!=TX_SUCCESS);assert(thread_comm_request_manual_abort()==TX_SUCCESS);return 0;}
 assert(thread_comm_admit_network_launch_original(message,"GS",2)==TX_SUCCESS);
 assert(thread_comm_admit_network_launch_original(message,"GS",2)==TX_SUCCESS);
 message.timestamp_ms=10001;assert(thread_comm_admit_network_launch_original(message,"GS",2)!=TX_SUCCESS);message.timestamp_ms=10000;
 assert(thread_comm_admit_network_launch_original(message,"X",1)!=TX_SUCCESS);
 uint32_t epoch=0;assert(thread_comm_receive_owned(&message,&epoch)==TX_SUCCESS);assert(thread_comm_launch_claim(message.timestamp_ms,epoch,&owner));assert(thread_comm_receive_owned(&message,&epoch)!=TX_SUCCESS);
"""
  code+=r"""
 assert(owner.source_utc==10000&&owner.serial!=0&&thread_comm_launch_continuity());
 if(!strcmp(argv[1],"stop-after-take")){assert(thread_comm_request_manual_abort()==TX_SUCCESS);assert(!thread_comm_launch_begin_owned(&owner,effect,0)&&!thread_comm_launch_effect_owned(&owner,effect,0)&&!effects&&!thread_comm_launch_continuity());return 0;}
 storage_ready=false;test_tamp.BKP28R=test_tamp.BKP29R=0;drop_launch=1;
 assert(thread_comm_launch_original_accepted(10000,"GS",2));
 thread_comm_launch_owner_t wrong=owner;wrong.serial++;assert(!thread_comm_launch_begin_owned(&wrong,effect,0));
 wrong=owner;wrong.source_utc++;assert(!thread_comm_launch_begin_owned(&wrong,effect,0));
 assert(!effects&&thread_comm_launch_begin_owned(&owner,effect,0)&&effects==1);
 assert(!thread_comm_launch_begin_owned(&owner,effect,0)&&effects==1);
 assert(thread_comm_launch_effect_owned(&owner,effect,0)&&effects==2);
 thread_comm_launch_complete_owned(&owner);assert(thread_comm_launch_continuity());
 assert(!thread_comm_launch_effect_owned(&owner,effect,0)&&effects==2);
 assert(thread_comm_launch_original_accepted(10000,"GS",2));
 assert(thread_comm_request_manual_abort()==TX_SUCCESS);assert(thread_comm_get_abort()&&!thread_comm_launch_continuity());
 assert(!thread_comm_launch_original_accepted(10000,"GS",2)&&!thread_comm_launch_begin_owned(&owner,effect,0)&&!thread_comm_launch_effect_owned(&owner,effect,0)&&effects==2);
 return 0;
}
"""
  compile_case(code,'initial-failure')
  compile_case(code,'receipt-lifecycle')
  compile_case(code,'stop-after-take')

 def test_checked_local_serial_exhaustion_refuses_without_marker_or_owner(self):
  pre=re.sub(r'#ifdef VB\n.*?#endif','',prefix(),flags=re.S)
  code=pre+'\n#include "'+str((ROOT/'Core/Src/thread_comm.c').resolve())+'"\n'+r"""
int main(int argc,char**argv){
 (void)argv;assert(argc==2);board_abort_latch_init();TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==TX_SUCCESS);
 g_launch_serial=UINT32_MAX-1U;
"""
  code+=r"""
 thread_comm_msg_t original={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=10000};
 assert(thread_comm_admit_network_launch_original(original,"GS",2)!=TX_SUCCESS);
 thread_comm_msg_t output;assert(thread_comm_receive(&output,TX_NO_WAIT)!=TX_SUCCESS);
"""
  code+=r"""
 assert(!thread_comm_launch_continuity()&&!board_launch_latch_is_set()&&!thread_comm_get_abort());
 assert(g_launch_serial==UINT32_MAX-1U);fprintf(stderr,"HOST_LAYOUT receipt=%zu serial=%zu continuity=%zu token=%zu ring_entry=%zu ULONG=%zu\n",sizeof g_accepted_launch,sizeof g_launch_serial,sizeof g_launch_continuity,sizeof(thread_comm_launch_owner_t),sizeof(thread_comm_msg_t),sizeof(ULONG));return 0;
}
"""
  result=compile_case(code,'exhausted',inline_state=True)
  print(result.stderr.strip())

 def test_invalid_recovery_epoch_never_authorizes_launch_but_manual_stop_remains_safe(self):
  pre=re.sub(r'#ifdef VB\n.*?#endif','',prefix(),flags=re.S)
  code=pre+'\n#include "'+str((ROOT/'Core/Src/thread_comm.c').resolve())+'"\n'+r"""
static unsigned safe_off;
static void manual_safe_off(void){safe_off++;}
int main(int argc,char**argv){
 assert(argc==2);board_abort_latch_init();TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==TX_SUCCESS);
 assert(thread_comm_bind_manual_stop_apply(manual_safe_off)==TX_SUCCESS);
 g_thread_comm_recovery_epoch=!strcmp(argv[1],"zero")?0U:UINT32_MAX;
"""
  code+=r"""
 thread_comm_msg_t original={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=10000};
 assert(thread_comm_admit_network_launch_original(original,"GS",2)!=TX_SUCCESS);
 thread_comm_msg_t output;assert(thread_comm_receive(&output,TX_NO_WAIT)!=TX_SUCCESS);
"""
  code+=r"""
 assert(!thread_comm_launch_continuity()&&!board_launch_latch_is_set()&&!thread_comm_get_abort()&&!safe_off);
 storage_ready=false;assert(thread_comm_request_manual_abort()==TX_SUCCESS);
 assert(thread_comm_get_abort()&&safe_off==1&&!thread_comm_launch_continuity());return 0;
}
"""
  compile_case(code,'zero',inline_state=True)
  compile_case(code,'max',inline_state=True)

 def test_initial_uncertain_release_never_mints_accepted_history(self):
  pre=re.sub(r'#ifdef VB\n.*?#endif','',failure_prefix(),flags=re.S)
  code=pre+'\n#include "'+str((ROOT/'Core/Src/thread_comm.c').resolve())+'"\n'+r"""
int main(int argc,char**argv){
 (void)argv;assert(argc==2);board_abort_latch_init();TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==TX_SUCCESS);
 fail_next_state_release=true;
"""
  code+=r"""
 thread_comm_msg_t original={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=10000};
 assert(thread_comm_admit_network_launch_original(original,"GS",2)==THREAD_COMM_COMMIT_UNCERTAIN);
 uint32_t epoch=0;if(thread_comm_receive_owned(&original,&epoch)==TX_SUCCESS){thread_comm_launch_owner_t owner={0};assert(!thread_comm_launch_claim(original.timestamp_ms,epoch,&owner));}
"""
  code+=r"""
 assert(!g_accepted_launch.initial_verified&&g_accepted_launch.phase==LAUNCH_NONE&&!thread_comm_launch_continuity());
 assert(!thread_comm_launch_original_accepted(10000,"GS",2));assert(!thread_comm_get_abort());return 0;
}
"""
  compile_case(code,'initial-uncertain',inline_state=True)

 def test_later_duplicate_or_distinct_release_error_never_revokes_prior_accepted_owner(self):
  pre=re.sub(r'#ifdef VB\n.*?#endif','',failure_prefix(),flags=re.S)
  code=pre+'\n#include "'+str((ROOT/'Core/Src/thread_comm.c').resolve())+'"\n'+r"""
static unsigned effects,safe_off;
static void effect(void*context){(void)context;effects++;}
static void stop(void){safe_off++;}
int main(int argc,char**argv){
 assert(argc==2);board_abort_latch_init();TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==TX_SUCCESS);assert(thread_comm_bind_manual_stop_apply(stop)==TX_SUCCESS);
 thread_comm_launch_owner_t owner={0};uint64_t offered=!strcmp(argv[1],"distinct")?10001:10000;
"""
  code+=r"""
 thread_comm_msg_t original={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=10000};assert(thread_comm_admit_network_launch_original(original,"GS",2)==TX_SUCCESS);
 fail_next_state_release=true;thread_comm_msg_t attempt={.cmd=CMD_IGNITER_SEQUENCE,.timestamp_ms=offered};assert(thread_comm_admit_network_launch_original(attempt,"GS",2)==THREAD_COMM_COMMIT_UNCERTAIN);
 assert(thread_comm_launch_continuity()&&thread_comm_launch_original_accepted(10000,"GS",2));uint32_t epoch=0;assert(thread_comm_receive_owned(&original,&epoch)==TX_SUCCESS);assert(original.timestamp_ms==10000&&thread_comm_launch_claim(original.timestamp_ms,epoch,&owner));
"""
  code+=r"""
 assert(owner.source_utc==10000&&owner.epoch==thread_comm_manual_epoch()&&owner.serial==1&&g_accepted_launch.initial_verified);
 storage_ready=false;assert(thread_comm_launch_begin_owned(&owner,effect,0)&&effects==1);assert(thread_comm_launch_effect_owned(&owner,effect,0)&&effects==2);
 assert(thread_comm_request_manual_abort()==TX_SUCCESS&&safe_off==1&&!thread_comm_launch_continuity());assert(!thread_comm_launch_effect_owned(&owner,effect,0)&&effects==2);return 0;
}
"""
  compile_case(code,'duplicate',inline_state=True)
  compile_case(code,'distinct',inline_state=True)
