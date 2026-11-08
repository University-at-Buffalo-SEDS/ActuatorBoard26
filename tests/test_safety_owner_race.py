"""A late safety producer cannot wipe a later recovered Launch.

Actual safety branches, retained state/queue/recovery, output-safe routine,
recovery completion and Launch sequence arming; host MMIO/SDK/NVM boundaries.
This is a software interleaving proof, not a physical safety latency test.
"""
from pathlib import Path
import ast,os,subprocess,tempfile,unittest
from test_launch_abort_policy import function
ROOT=Path(os.environ.get("AB_SAFETY_OWNER_SOURCE",Path(__file__).resolve().parents[1]))

def block(source,marker):
 start=source.index(marker);brace=source.index('{',start);end=brace+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]

class SafetyOwnerRaceTests(unittest.TestCase):
 def test_three_late_producers_and_current_owner_stop(self):
  strings=[n.value for n in ast.walk(ast.parse((ROOT/'tests/test_board_recovery_owner.py').read_text()))if isinstance(n,ast.Constant)and isinstance(n.value,str)]
  tx=next(s for s in strings if 'typedef unsigned UINT;'in s)
  code=next(s for s in strings if 'PRODUCTION'in s and 'int main(void)'in s)
  code=code[:code.index('int main(void)')].replace('PRODUCTION',(ROOT/'Core/Src/thread_comm.c').read_text())
  code=code.replace('static void finish(void){','static void recovery_finish_owner(void);\nstatic void finish(void){recovery_finish_owner();')
  code+=r'''
#include "safety_diagnostics.h"
#include <stddef.h>
#include <string.h>

static uint8_t g_aborted,g_igniter_on,g_nitrogen_open,g_nitrous_open;
static uint8_t g_launch_sequence_active,g_launch_sequence_igniter_started,g_launch_sequence_holdoff_active,g_plumbing_retract_active;
static uint32_t g_plumbing_retract_steps_remaining;
static ULONG g_launch_sequence_igniter_start_ticks,g_launch_sequence_igniter_stop_ticks;
static unsigned gpio_writes,writes_when_relaunched,recoveries,interleave=1;
static uint64_t now_ms,safety_heartbeat_missing_since_ms;
static uint32_t safety_heartbeat_timeout_count;
static volatile uint32_t g_safety_adc_start_failures;static volatile uint8_t g_safety_adc_warning_pending;
typedef struct{uint32_t ODR;}Port;static Port ports[11];static int stepper;
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
#define LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS 10000U
#define SAFETY_HEARTBEAT_TIMEOUT_MS 5000ULL
#define SAFETY_CHECK_PERIOD_TICKS 50U
#define CMD_NITROGEN_OPEN 9
#define CMD_NITROUS_OPEN 12
#define CMD_IGNITER_ON 6
PORTS
static void HAL_GPIO_WritePin(Port*p,unsigned pin,unsigned value){gpio_writes++;if(value)p->ODR|=pin;else p->ODR&=~pin;}
static unsigned stepper_sleeps;static void stepperSleep(int*p){(void)p;stepper_sleeps++;}
/* This late-producer fixture exercises the real finish callbacks with a
 * conservatively unready selector. Actual driver rearm is tested separately. */
static uint32_t g_fill_start_serial;
static uint8_t fill_settings_mode(void){return 255U;}
static bool fill_settings_ready(void){return false;}
static bool stepperIsStoppedAsleep(int*p){(void)p;return false;}
static uint8_t recovery_read_outputs(uint32_t*mask){return read_outputs(mask);}
static void publish_expected_outputs(void){}
static int publish_umbilical_status(unsigned cmd,unsigned value){(void)cmd;(void)value;return 0;}
static void publish_umbilical_statuses_if_due(ULONG*p){(void)p;}
static void retry_pending_status_reports(void){}
static UINT tx_thread_sleep(ULONG ticks){(void)ticks;return 0;}
static ULONG ms_to_ticks(uint32_t ms){return ms;}
ULONG tx_time_get(void){return 100U;}
static uint32_t fixture_launch_tick(void){return (uint32_t)tx_time_get();}
ACTUAL_FILL_HELPERS
ACTUAL_FORCE
ACTUAL_RECOVERY_FINISH
ACTUAL_START
static void owner_abort_pass(void){thread_comm_msg_t msg;uint32_t command_epoch=0;ULONG last_umbilical_status_ticks=0;do ACTUAL_OWNER_ABORT while(0);}
static uint8_t local_and_interleave(uint32_t reason,uint64_t now,uint64_t heartbeat,uint32_t state,uint32_t detail,uint64_t original_utc,uint8_t heartbeat_only){
 uint8_t claimed=thread_comm_request_local_abort(reason,now,heartbeat,state,detail,original_utc,heartbeat_only);
 if(!claimed||!interleave)return claimed;
 /* The real owner first consumes the retained Abort, then serially completes
  * recovery, then a NEW admitted Launch arms the actual sequence function. */
 assert(thread_comm_get_abort());owner_abort_pass();assert(!g_launch_sequence_active);
 BoardRecoverySnapshot snapshot;assert(board_recovery_snapshot(&snapshot)==0);
 assert(board_recovery_begin_prepare(0,snapshot.abort_epoch)==0);
 assert(board_recovery_apply_safe(0,snapshot.abort_epoch,&snapshot)==-14);
 board_recovery_execute_owned();assert(!thread_comm_get_abort()&&!g_aborted);
 thread_comm_msg_t launch={.cmd=14,.timestamp_ms=10000};assert(thread_comm_admit_network_launch_original_at(launch,"GS",2U,10000U,fixture_launch_tick(),fixture_launch_tick)==TX_SUCCESS);
 uint32_t launch_epoch=0;assert(thread_comm_receive_owned(&launch,&launch_epoch)==TX_SUCCESS);start_launch_sequence(launch.timestamp_ms,launch_epoch);assert(g_launch_sequence_active&&board_launch_latch_is_set());
 HAL_GPIO_WritePin(&ports[0],1,GPIO_PIN_SET);writes_when_relaunched=gpio_writes;recoveries++;return claimed;
}
static uint64_t telemetry_event_clock_snapshot(void){return 9000;}
static uint64_t safety_now_ms(void){return now_ms;}
static UINT safety_adc_start(void){return TX_QUEUE_ERROR;}
#define thread_comm_request_local_abort local_and_interleave
ACTUAL_HEARTBEAT
static void adc_failure_once(void){do ACTUAL_ADC_FAILURE while(0);}
#undef thread_comm_request_local_abort
int main(int argc,char**argv){
 assert(argc==2);TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==0);board_recovery_bind_owner(read_outputs,prepare,finish);
 assert(thread_comm_set_flight_state(ACTUATOR_FLIGHT_STATE_PREFILL)==TX_SUCCESS);
 if(!strcmp(argv[1],"fresh_prelaunch")){interleave=0;ports[0].ODR=1;g_igniter_on=1;now_ms=5100;assert(thread_comm_note_groundstation_heartbeat(100)==0);safety_check_heartbeat();assert(thread_comm_get_abort());unsigned before_owner=gpio_writes;owner_abort_pass();assert(gpio_writes>before_owner&&!ports[0].ODR&&!g_igniter_on&&!g_launch_sequence_active&&g_safety_first_abort.reason==SAFETY_ABORT_HEARTBEAT);return 0;}
 if(!strcmp(argv[1],"overpressure_motion")){
  /* Overpressure in Startup with retraction mid-motion and every output on. */
  interleave=0;assert(thread_comm_set_flight_state(ACTUATOR_FLIGHT_STATE_STARTUP)==TX_SUCCESS);
  for(unsigned i=0;i<11;i++)ports[i].ODR=1;
  g_igniter_on=g_nitrogen_open=g_nitrous_open=1;g_launch_sequence_active=0;
  g_plumbing_retract_active=1;g_plumbing_retract_steps_remaining=500;
  g_fill_motion.active=true;g_fill_motion.active_action=1;
  assert(thread_comm_request_automatic_abort(0U)==0&&!thread_comm_get_abort()); /* generic: Startup excluded */
  assert(thread_comm_request_automatic_abort(1U)==1&&thread_comm_get_abort());
  owner_abort_pass();
  for(unsigned i=0;i<11;i++)if(i!=6)assert(!ports[i].ODR);
  assert(stepper_sleeps>=1&&!g_plumbing_retract_active&&!g_plumbing_retract_steps_remaining&&!g_fill_motion.active);
  assert(!g_igniter_on&&!g_nitrogen_open&&!g_nitrous_open&&!g_launch_sequence_active);
  return 0;
 }
 if(!strcmp(argv[1],"heartbeat")){now_ms=5100;assert(thread_comm_note_groundstation_heartbeat(100)==0);safety_check_heartbeat();}
 else if(!strcmp(argv[1],"initial_heartbeat")){now_ms=5100;safety_heartbeat_missing_since_ms=100;safety_check_heartbeat();}
 else if(!strcmp(argv[1],"adc_start")){
  /* ADC start failure is only a warning: no Abort, no recovery race. */
  now_ms=100;adc_failure_once();
  assert(!thread_comm_get_abort()&&recoveries==0);
  assert(g_safety_adc_warning_pending==1U&&g_safety_adc_start_failures==1U);
  assert(g_safety_first_abort.reason!=SAFETY_ABORT_ADC_START);
  return 0;
 }
 else assert(0);
 assert(recoveries==1&&finished==1);assert(!thread_comm_get_abort());
 assert(g_launch_sequence_active&&ports[0].ODR==1&&gpio_writes==writes_when_relaunched);
 assert(g_safety_first_abort.reason==(!strcmp(argv[1],"heartbeat")?SAFETY_ABORT_HEARTBEAT:SAFETY_ABORT_INITIAL_HEARTBEAT));
 /* A genuinely NEW manual Stop after Launch still acts through this owner. */
 assert(thread_comm_request_manual_abort()==TX_SUCCESS);assert(thread_comm_get_abort());owner_abort_pass();assert(!g_launch_sequence_active&&!ports[0].ODR);
 return 0;
}
'''
  ports=''
  for i,name in enumerate(['IGNITER_PIN','NITROGEN_PIN','N2_SIG','NITROUS_PIN','N20_SIG','BACKUP_VALVE_EN','STEPPER_SLEEP','N_FWD_EN','N_BCKWD_EN','P_FWD_EN','P_BCKWD_EN']):ports+=f'#define {name}_GPIO_Port (&ports[{i}])\n#define {name}_Pin 1U\n'
  owner=(ROOT/'Core/Src/main_task.c').read_text();safety=(ROOT/'Core/Src/safety_task.c').read_text()
  fill_helpers='#include \"fill_motion.h\"\nstatic FillMotion g_fill_motion;static bool g_fill_drive_fault;\n'+(ROOT/'Core/Src/fill_motion.c').read_text()+''.join(function(owner,name) for name in ['all_fill_outputs_off','fill_outputs_are_off','fill_stop_locked'])
  fill_helpers='#include \"fill_motion_status.h\"\nstatic FillMotionProvenance g_fill_provenance;\n'+fill_helpers+'\n'+(ROOT/'Core/Src/fill_motion_status.c').read_text()
  for key,value in [('ACTUAL_FILL_HELPERS',fill_helpers),('PORTS',ports),('ACTUAL_FORCE',function(owner,'main_task_force_outputs_safe_off')),('ACTUAL_RECOVERY_FINISH',function(owner,'recovery_finish_base')+function(owner,'recovery_fill_rearm_ready')+function(owner,'recovery_finish_owner')),('ACTUAL_START','static uint32_t g_launch_manual_epoch;\nstatic thread_comm_launch_owner_t g_launch_owner;\n'+__import__('re').search(r'typedef struct\{[^\n]*\} LaunchStart;',owner).group(0)+'\n'+function(owner,'start_launch_sequence_locked')+'\n'+function(owner,'start_launch_sequence')),('ACTUAL_OWNER_ABORT',block(owner[owner.index('void main_task_entry('):],'if (thread_comm_get_abort() != 0U)\n        {')),('ACTUAL_HEARTBEAT',function(safety,'safety_check_heartbeat')),('ACTUAL_ADC_FAILURE',block(safety,'if (safety_adc_start() != TX_SUCCESS)'))]:code=code.replace(key,value)
  with tempfile.TemporaryDirectory(prefix='safety-owner-race-')as tmp:
   p=Path(tmp);(p/'tx_api.h').write_text(tx);(p/'test.c').write_text(code);binary=p/'test'
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(ROOT/'Core/Inc'),str(p/'test.c'),'-o',str(binary)],check=True)
   for case in ['heartbeat','initial_heartbeat','adc_start','fresh_prelaunch','overpressure_motion']:
    with self.subTest(case=case):subprocess.run([str(binary),case],check=True)
if __name__=='__main__':unittest.main()
