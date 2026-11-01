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
volatile safety_abort_diagnostic_t g_safety_first_abort;
static uint8_t g_aborted,g_igniter_on,g_nitrogen_open,g_nitrous_open;
static uint8_t g_launch_sequence_active,g_launch_sequence_igniter_started,g_launch_sequence_holdoff_active,g_plumbing_retract_active;
static uint32_t g_plumbing_retract_steps_remaining;
static ULONG g_launch_sequence_igniter_start_ticks,g_launch_sequence_igniter_stop_ticks;
static unsigned gpio_writes,writes_when_relaunched,recoveries,interleave=1;
static uint64_t now_ms,safety_heartbeat_missing_since_ms;
static uint32_t safety_heartbeat_timeout_count,safety_adc_start_step=2;
typedef struct{uint32_t ODR;}Port;static Port ports[7];static int stepper;
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
static void stepperSleep(int*p){(void)p;}
static void publish_expected_outputs(void){}
static int publish_umbilical_status(unsigned cmd,unsigned value){(void)cmd;(void)value;return 0;}
static void publish_umbilical_statuses_if_due(ULONG*p){(void)p;}
static void retry_pending_status_reports(void){}
static UINT tx_thread_sleep(ULONG ticks){(void)ticks;return 0;}
static ULONG ms_to_ticks(uint32_t ms){return ms;}
static ULONG launch_sequence_igniter_start_ticks(uint64_t timestamp){(void)timestamp;return 10000;}
ACTUAL_FORCE
ACTUAL_RECOVERY_FINISH
ACTUAL_START
static void owner_abort_pass(void){thread_comm_msg_t msg;ULONG last_umbilical_status_ticks=0;do ACTUAL_OWNER_ABORT while(0);}
static void record_and_interleave(uint32_t reason,uint64_t now,uint64_t heartbeat,uint32_t state,uint32_t detail,uint32_t before){
 safety_record_first_abort(reason,now,heartbeat,state,detail,before);
 if(!interleave)return;
 /* The real owner first consumes the retained Abort, then serially completes
  * recovery, then a NEW admitted Launch arms the actual sequence function. */
 assert(thread_comm_get_abort());owner_abort_pass();assert(!g_launch_sequence_active);
 BoardRecoverySnapshot snapshot;assert(board_recovery_snapshot(&snapshot)==0);
 assert(board_recovery_begin_prepare(0,snapshot.abort_epoch)==0);
 assert(board_recovery_apply_safe(0,snapshot.abort_epoch,&snapshot)==-14);
 thread_comm_service_board_recovery();assert(!thread_comm_get_abort()&&!g_aborted);
 thread_comm_msg_t launch={.cmd=14,.timestamp_ms=10000};assert(thread_comm_admit_launch(launch)==TX_SUCCESS);
 start_launch_sequence(10000);assert(g_launch_sequence_active&&board_launch_latch_is_set());
 HAL_GPIO_WritePin(&ports[0],1,GPIO_PIN_SET);writes_when_relaunched=gpio_writes;recoveries++;
}
static uint64_t safety_now_ms(void){return now_ms;}
static UINT safety_adc_start(void){return TX_QUEUE_ERROR;}
#define safety_record_first_abort record_and_interleave
ACTUAL_HEARTBEAT
static void adc_failure_once(void){do ACTUAL_ADC_FAILURE while(0);}
#undef safety_record_first_abort
int main(int argc,char**argv){
 assert(argc==2);TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==0);board_recovery_bind_owner(read_outputs,prepare,finish);
 assert(thread_comm_set_flight_state(ACTUATOR_FLIGHT_STATE_PREFILL)==TX_SUCCESS);
 if(!strcmp(argv[1],"fresh_prelaunch")){interleave=0;ports[0].ODR=1;g_igniter_on=1;now_ms=5100;assert(thread_comm_note_groundstation_heartbeat(100)==0);safety_check_heartbeat();assert(thread_comm_get_abort());unsigned before_owner=gpio_writes;owner_abort_pass();assert(gpio_writes>before_owner&&!ports[0].ODR&&!g_igniter_on&&!g_launch_sequence_active&&g_safety_first_abort.reason==SAFETY_ABORT_HEARTBEAT);return 0;}
 if(!strcmp(argv[1],"heartbeat")){now_ms=5100;assert(thread_comm_note_groundstation_heartbeat(100)==0);safety_check_heartbeat();}
 else if(!strcmp(argv[1],"initial_heartbeat")){now_ms=5100;safety_heartbeat_missing_since_ms=100;safety_check_heartbeat();}
 else if(!strcmp(argv[1],"adc_start")){now_ms=100;adc_failure_once();}
 else assert(0);
 assert(recoveries==1&&finished==1);assert(!thread_comm_get_abort());
 assert(g_launch_sequence_active&&ports[0].ODR==1&&gpio_writes==writes_when_relaunched);
 assert(g_safety_first_abort.reason==(!strcmp(argv[1],"heartbeat")?SAFETY_ABORT_HEARTBEAT:!strcmp(argv[1],"initial_heartbeat")?SAFETY_ABORT_INITIAL_HEARTBEAT:SAFETY_ABORT_ADC_START));
 /* A genuinely NEW manual Stop after Launch still acts through this owner. */
 assert(thread_comm_request_manual_abort()==TX_SUCCESS);assert(thread_comm_get_abort());owner_abort_pass();assert(!g_launch_sequence_active&&!ports[0].ODR);
 return 0;
}
'''
  ports=''
  for i,name in enumerate(['IGNITER_PIN','NITROGEN_PIN','N2_SIG','NITROUS_PIN','N20_SIG','BACKUP_VALVE_EN','STEPPER_SLEEP']):ports+=f'#define {name}_GPIO_Port (&ports[{i}])\n#define {name}_Pin 1U\n'
  owner=(ROOT/'Core/Src/main_task.c').read_text();safety=(ROOT/'Core/Src/safety_task.c').read_text()
  for key,value in [('PORTS',ports),('ACTUAL_FORCE',function(owner,'main_task_force_outputs_safe_off')),('ACTUAL_RECOVERY_FINISH',function(owner,'recovery_finish_owner')),('ACTUAL_START',function(owner,'start_launch_sequence')),('ACTUAL_OWNER_ABORT',block(owner[owner.index('void main_task_entry('):],'if (thread_comm_get_abort() != 0U)\n        {')),('ACTUAL_HEARTBEAT',function(safety,'safety_check_heartbeat')),('ACTUAL_ADC_FAILURE',block(safety,'if (safety_adc_start() != TX_SUCCESS)'))]:code=code.replace(key,value)
  with tempfile.TemporaryDirectory(prefix='safety-owner-race-')as tmp:
   p=Path(tmp);(p/'tx_api.h').write_text(tx);(p/'test.c').write_text(code);binary=p/'test'
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(ROOT/'Core/Inc'),str(p/'test.c'),'-o',str(binary)],check=True)
   for case in ['heartbeat','initial_heartbeat','adc_start','fresh_prelaunch']:
    with self.subTest(case=case):subprocess.run([str(binary),case],check=True)
if __name__=='__main__':unittest.main()
