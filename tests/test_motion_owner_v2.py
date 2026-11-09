"""Actual state owner and production motion callbacks; peripheral boundaries only.

Deterministic wake interleavings are logic evidence, not physical latency tests.
"""
from pathlib import Path
from native_sdk import prepare_native_sdk
import ast,re,subprocess,tempfile,unittest
def function(source,name):
 match=re.search(r'\b'+re.escape(name)+r'\([^;{}]*\)\s*\{',source)
 assert match,name
 start=source.rfind('\n',0,match.start())+1;end=match.end();depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
ROOT=Path(__file__).resolve().parents[1]
class MotionOwnerTests(unittest.TestCase):
 def test_actual_wake_stop_new_owner_and_minimum_snapshots(self):
  strings=[n.value for n in ast.walk(ast.parse((ROOT/'tests/test_board_recovery_owner.py').read_text())) if isinstance(n,ast.Constant) and isinstance(n.value,str)]
  tx=next(s for s in strings if 'typedef unsigned UINT;' in s)
  code=next(s for s in strings if 'PRODUCTION' in s and 'int main(void)' in s)
  code=code[:code.index('int main(void)')].replace('PRODUCTION',(ROOT/'Core/Src/thread_comm.c').read_text())
  code+=r'''
#include <string.h>
#include "fill_motion.h"
#include "fill_motion_status.h"
static FillMotion g_fill_motion;static FillMotionProvenance g_fill_provenance;
static bool g_fill_drive_fault;static uint32_t g_fill_start_serial;
static uint8_t g_plumbing_retract_active,g_plumbing_retracted;static uint32_t g_plumbing_retract_steps_remaining;
static uint8_t mode;static bool settings_ready=true,wake_ok=true,timer_ok=true,driver_active;
static uint32_t tick;static uint64_t settings_cutoff=9000,network_now=10000;
static unsigned pulses_started,sleeps,wakes,gpio_on;static void(*wake_interleave)(void);
static int stepper;
typedef struct {uint32_t ODR;}Port;static Port ports[5];
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define STEP_OK 0
#define STEP_CW 0
#define CMD_RETRACT_PLUMBING 8
#define PLUMBING_RETRACT_STEPS 20000000U
#define PLUMBING_RETRACT_STEP_PERIOD_US 30U
PORTS
static void HAL_GPIO_WritePin(Port*p,unsigned pin,unsigned value){assert(g_thread_comm_state_mutex.locked || value==0U);if(value){p->ODR|=pin;gpio_on++;}else p->ODR&=~pin;}
static bool fill_settings_ready(void){return settings_ready;}
static uint8_t fill_settings_mode(void){return mode;}
static uint64_t fill_settings_cutoff(void){return settings_cutoff;}
static uint64_t telemetry_unix_ms(void){return network_now;}
static unsigned publish_umbilical_status(unsigned cmd,unsigned value){(void)cmd;(void)value;return 0;}
static void stepperSetDir(int*p,unsigned d){(void)p;(void)d;assert(!g_thread_comm_state_mutex.locked);}
static int stepperWakeBegin(int*p){(void)p;assert(g_thread_comm_state_mutex.locked);wakes++;return 0;}
static int stepperWakeWait(int*p){(void)p;assert(!g_thread_comm_state_mutex.locked);if(wake_interleave){void(*hook)(void)=wake_interleave;wake_interleave=0;hook();}return wake_ok?0:-1;}
static void stepperSleep(int*p){(void)p;assert(g_thread_comm_state_mutex.locked);driver_active=false;sleeps++;}
static int stepperStartMoveTimer(int*p,unsigned steps,unsigned period){(void)p;assert(g_thread_comm_state_mutex.locked&&!g_thread_comm_abort&&!abort_latch);assert(steps==20000000U&&period==30U);if(!timer_ok)return -1;pulses_started++;driver_active=true;return 0;}
static bool stepperIsMoveActive(int*p){(void)p;return driver_active;}
unsigned long tx_time_get(void){return tick;}
ACTUAL
QUERY_ACTUAL
static void stop_in_wake(void){assert(thread_comm_request_manual_abort()==0);}
static void newer_in_wake(void){start_plumbing_retract(10001,thread_comm_manual_epoch());assert(pulses_started==1&&g_fill_provenance.source==10001);}
static void stop_then_recover_newer_in_wake(void){
 assert(thread_comm_request_manual_abort()==0);BoardRecoverySnapshot s;assert(board_recovery_snapshot(&s)==0);
 assert(board_recovery_begin_prepare(0,s.abort_epoch)==0);assert(board_recovery_apply_safe(0,s.abort_epoch,&s)==-14);board_recovery_execute_owned();assert(!thread_comm_get_abort());
 start_plumbing_retract(10002,thread_comm_manual_epoch());assert(driver_active&&g_fill_provenance.source==10002);
}
static FillMotionSnapshotV2 snapshot(void){FillMotionSnapshotV2 s;assert(main_task_fill_snapshot_v2(&s));return s;}
int main(int argc,char**argv){
 assert(argc==2);TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==0);board_recovery_bind_owner(read_outputs,prepare,finish);tick=100;mode=FILL_MODE_STEPPER;
 if(!strcmp(argv[1],"stop_wake")){wake_interleave=stop_in_wake;start_plumbing_retract(10000,thread_comm_manual_epoch());assert(wakes==1&&sleeps==1&&!pulses_started&&!driver_active&&thread_comm_get_abort());assert(snapshot().lifecycle!=FILL_PROVENANCE_COMPLETED);}
 else if(!strcmp(argv[1],"newer_wake")||!strcmp(argv[1],"stop_recover_newer")){wake_interleave=!strcmp(argv[1],"newer_wake")?newer_in_wake:stop_then_recover_newer_in_wake;start_plumbing_retract(10000,thread_comm_manual_epoch());assert(pulses_started==1&&wakes==2&&sleeps==0&&driver_active&&g_fill_provenance.source>10000);assert(snapshot().busy&&snapshot().lifecycle==FILL_PROVENANCE_ACTIVE);}
 else if(!strcmp(argv[1],"legacy_minimum")){
 start_selected_plumbing_retract(10000,thread_comm_manual_epoch());assert(pulses_started==1);FillMotionSnapshotV2 s=snapshot();assert(s.accepted_source==10000&&s.origin_epoch==s.current_epoch&&s.minimum_remaining_ms==45000&&s.busy);
 start_selected_plumbing_retract(10000,thread_comm_manual_epoch());start_selected_plumbing_retract(10001,thread_comm_manual_epoch());assert(wakes==1&&pulses_started==1&&g_fill_provenance.source==10000);
 tick=45100;assert(snapshot().minimum_remaining_ms==1&&snapshot().busy);tick=45101;assert(snapshot().minimum_remaining_ms==0&&snapshot().busy&&sleeps==0);
 driver_active=false;service_plumbing_retract();s=snapshot();assert(!s.busy&&s.lifecycle==FILL_PROVENANCE_COMPLETED&&s.minimum_remaining_ms==0&&sleeps==1);
 invalidate_motion_provenance_locked(false,0);assert(snapshot().lifecycle==FILL_PROVENANCE_CANCELLED);
 }
 else if(!strcmp(argv[1],"legacy_early_complete")){
 start_selected_plumbing_retract(10000,thread_comm_manual_epoch());tick=200;driver_active=false;service_plumbing_retract();assert(snapshot().lifecycle==FILL_PROVENANCE_COMPLETED&&!snapshot().busy&&snapshot().minimum_remaining_ms==44901);
 assert(thread_comm_request_manual_abort()==0);assert(snapshot().lifecycle==FILL_PROVENANCE_CANCELLED);
 }
 else if(!strcmp(argv[1],"aux_minimum")){
 mode=FILL_MODE_AUX;start_selected_plumbing_retract(10000,thread_comm_manual_epoch());assert(gpio_on==1&&g_fill_motion.active&&g_fill_provenance.source==10000);
 tick=200;start_selected_plumbing_retract(10000,thread_comm_manual_epoch());network_now=10001;start_selected_plumbing_retract(10001,thread_comm_manual_epoch());assert(gpio_on==1&&g_fill_motion.started_ms==100&&g_fill_provenance.source==10000);
 tick=45100;service_auxiliary_retract();assert(snapshot().lifecycle==FILL_PROVENANCE_COMPLETED&&!snapshot().busy&&snapshot().minimum_remaining_ms==1&&!ports[0].ODR);
 tick=45101;assert(snapshot().minimum_remaining_ms==0);assert(thread_comm_request_manual_abort()==0);assert(snapshot().lifecycle==FILL_PROVENANCE_CANCELLED);
 }
 else if(!strcmp(argv[1],"stop_before_service")){
 mode=FILL_MODE_AUX;start_selected_plumbing_retract(10000,thread_comm_manual_epoch());tick=45101;assert(thread_comm_request_manual_abort()==0);service_auxiliary_retract();assert(!g_fill_motion.active&&!ports[0].ODR&&snapshot().lifecycle==FILL_PROVENANCE_CANCELLED);
 }
 else if(!strcmp(argv[1],"timer_failure")){timer_ok=false;start_selected_plumbing_retract(10000,thread_comm_manual_epoch());FillMotionSnapshotV2 s;assert(!main_task_fill_snapshot_v2(&s)&&s.lifecycle==FILL_PROVENANCE_FAULT&&!pulses_started);}
 else if(!strcmp(argv[1],"owner_busy")){g_thread_comm_state_mutex.locked=1;start_selected_plumbing_retract(10000,thread_comm_manual_epoch());FillMotionSnapshotV2 s;assert(!main_task_fill_snapshot_v2(&s)&&s.mode==255&&s.minimum_remaining_ms==45000&&!wakes&&!pulses_started);g_thread_comm_state_mutex.locked=0;}
 else if(!strcmp(argv[1],"query_wire")){
 mode=FILL_MODE_AUX;start_selected_plumbing_retract(10000,thread_comm_manual_epoch());uint8_t q[33]={2,0};q[4]=55;q[24]=77;uint32_t ep=SEDS_EP_ACTUATOR_BOARD;SedsPacketView p={.ty=SEDS_DT_AB_FILL_DRIVE_SETTINGS,.sender="GS",.sender_len=2,.endpoints=&ep,.num_endpoints=1,.payload=q,.payload_len=32,.timestamp=10000};SedsRouter*r=(SedsRouter*)&ep;
 assert(fill_settings_handle(r,&p)==SEDS_OK&&reported_type==SEDS_DT_AB_FILL_MOTION_STATUS_V2&&reported_count==56&&reply[0]==2&&reply[1]==0&&reply[4]==55&&reply[24]==77&&reply[32]==1&&reply[33]==1&&reply[34]==1&&reply[35]==1&&reply[40]==0x10&&reply[41]==0x27&&reply[52]==1);
 uint8_t old[56];memcpy(old,reply,56);q[0]=1;assert(fill_settings_handle(r,&p)==0&&reported_type==SEDS_DT_AB_FILL_DRIVE_STATUS&&reported_count==40&&reply[0]==1&&reply[34]==0&&reply[35]==0);assert(!memcmp(reply+4,old+4,28)&&!memcmp(reply+36,old+36,4));
 q[0]=2;q[1]=1;unsigned before=reports;assert(fill_settings_handle(r,&p)==SEDS_BAD_ARG&&reports==before);q[1]=0;
 p.payload_len=31;assert(fill_settings_handle(r,&p)==SEDS_BAD_ARG&&reports==before);p.payload_len=33;assert(fill_settings_handle(r,&p)==SEDS_BAD_ARG&&reports==before);p.payload_len=32;
 q[3]=1;assert(fill_settings_handle(r,&p)==SEDS_BAD_ARG);q[3]=0;q[4]=0;assert(fill_settings_handle(r,&p)==SEDS_BAD_ARG);q[4]=55;p.sender="PB";assert(fill_settings_handle(r,&p)==SEDS_PERMISSION_DENIED);p.sender="GS";
 tick=45101;service_auxiliary_retract();assert(fill_settings_handle(r,&p)==0&&reply[34]==2&&reply[33]==0&&!reply[48]&&!reply[49]&&!reply[50]&&!reply[51]);
 assert(thread_comm_request_manual_abort()==0);assert(fill_settings_handle(r,&p)==0&&reply[34]==3&&reply[52]!=reply[20]);
 assert(gpio_on==1&&wakes==0&&pulses_started==0);
 }
 else if(!strcmp(argv[1],"stale")){assert(thread_comm_publish_command_cutoff(10000)==0);start_selected_plumbing_retract(10000,thread_comm_manual_epoch());assert(!wakes&&!pulses_started);}
 else assert(0);
 assert(!ports[1].ODR&&!ports[2].ODR&&!ports[3].ODR&&!ports[4].ODR);return 0;
}
'''
  settings=(ROOT/'Core/Src/fill_settings.c').read_text()
  query=r'''
#include "sedsnet_config.h"
#define selected_ready settings_ready
#define selected_mode mode
#define selected_cutoff settings_cutoff
static uint8_t reply[56];static size_t reported_count;static SedsDataType reported_type;static unsigned reports;
static struct {uint8_t bytes[32],result;uint64_t source;bool valid,pending,dirty;SedsRouter*router;}cached;
static bool reconcile_pending,absent_candidate;
static bool cache_enter(void){assert(!"QUERY must not enter settings owner");return false;}
static bool cache_leave(void){assert(!"QUERY must not leave settings owner");return false;}
uint64_t board_recovery_boot_current(void){return 42;}
bool seds_router_sender_matches_hostname(const SedsRouter*r,const char*s,size_t n,const char*owner,size_t on){(void)r;return n==on&&!memcmp(s,owner,n);}
SedsResult seds_router_get_network_time_ms(SedsRouter*r,uint64_t*out){(void)r;*out=network_now;return SEDS_OK;}
uint64_t board_recovery_station_now(SedsRouter*r){uint64_t t=0;return seds_router_get_network_time_ms(r,&t)?0U:t;}
SedsResult seds_router_log_queue_typed(SedsRouter*r,SedsDataType ty,const void*data,size_t n,size_t size,SedsElemKind kind){(void)r;assert(!g_thread_comm_state_mutex.locked&&size==1&&kind==SEDS_EK_UNSIGNED&&(n==40||n==56));memcpy(reply,data,n);reported_count=n;reported_type=ty;reports++;return 0;}
bool main_task_fill_snapshot(uint8_t*m,uint8_t*a,uint8_t*b,uint32_t*remaining){FillMotionSnapshotV2 s;bool valid=main_task_fill_snapshot_v2(&s);*m=s.mode;*a=s.active_action;*b=s.busy;*remaining=s.remaining_ms;return valid;}
'''+''.join(function(settings,name)+'\n' for name in ['decode64','encode64','fresh','decode32','encode32','emit_motion_v2','emit','fill_settings_handle'])
  code=code.replace('QUERY_ACTUAL',query)
  owner=(ROOT/'Core/Src/main_task.c').read_text()
  names=['all_fill_outputs_off','fill_outputs_are_off','fill_stop_locked','fill_monotonic_ms','fill_minimum_start_ms','next_fill_serial','prepare_legacy_start','wake_legacy_start','finish_legacy_start','start_plumbing_retract','start_aux_locked','start_selected_plumbing_retract','service_aux_locked','service_auxiliary_retract','service_legacy_locked','service_plumbing_retract','invalidate_motion_provenance_locked','fill_snapshot_v2_locked','main_task_fill_snapshot_v2']
  actual='typedef struct {uint64_t source,now;uint32_t epoch,serial;uint8_t result;bool ticket,woke;} FillStart;\n'+''.join(function(owner,name)+'\n' for name in names)
  ports=''.join(f'#define {name}_GPIO_Port (&ports[{i}])\n#define {name}_Pin 1U\n' for i,name in enumerate(['BACKUP_VALVE_EN','N_FWD_EN','N_BCKWD_EN','P_FWD_EN','P_BCKWD_EN']))
  code=code.replace('ACTUAL',actual).replace('PORTS',ports)
  sdk=prepare_native_sdk(ROOT)
  with tempfile.TemporaryDirectory(prefix='motion-actual-owner-')as d:
   p=Path(d);(p/'tx_api.h').write_text(tx);(p/'test.c').write_text(code);exe=p/'test'
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DTELEMETRY_ENABLED','-I'+str(Path(sdk['header']).parent),'-fsanitize=address,undefined','-I'+str(p),'-I'+str(ROOT/'Core/Inc'),str(p/'test.c'),str(ROOT/'Core/Src/fill_motion.c'),str(ROOT/'Core/Src/fill_motion_status.c'),'-o',str(exe)],check=True)
   for case in ['stop_wake','newer_wake','stop_recover_newer','legacy_minimum','legacy_early_complete','aux_minimum','stop_before_service','timer_failure','owner_busy','stale','query_wire']:
    with self.subTest(case=case):subprocess.run([str(exe),case],check=True)
if __name__=='__main__':unittest.main()
