#include <string.h>
#include <stdio.h>
#include <sched.h>
#include "stepper_driver.h"
#include "fill_motion.h"
#include "fill_motion_status.h"
static FillMotion g_fill_motion;static FillMotionProvenance g_fill_provenance;
static bool g_fill_drive_fault;static uint32_t g_fill_start_serial;
static uint8_t g_plumbing_retract_active,g_plumbing_retracted;static uint32_t g_plumbing_retract_steps_remaining;
static uint8_t g_aborted,g_launch_sequence_active,g_launch_sequence_igniter_started,g_launch_sequence_holdoff_active,g_igniter_on,g_nitrogen_open,g_nitrous_open;
static uint8_t mode;static bool settings_ready=true;static uint32_t tick;
static uint64_t settings_cutoff=9000,network_now=10000;
static unsigned pulses_started,sleeps,wakes;static bool fail_config,fail_start,stuck_sleep,stuck_step,stuck_timer,fail_stop;
static void(*wake_interleave)(void);static void(*stop_interleave)(void);static unsigned cleanup_stops;
static GPIO_TypeDef ports[14];TIM_TypeDef fake_tim1;TIM_HandleTypeDef htim1={&fake_tim1};
CoreDebug_Type fake_core_debug;uint32_t SystemCoreClock=170000000U;
static DWT_Type fake_cycles;
DWT_Type *fake_dwt(void){fake_cycles.CYCCNT+=1700;return &fake_cycles;}
static stepper_t stepper={.step_port=&ports[5],.step_pin=1,.dir_port=&ports[6],.dir_pin=1,.reset_port=&ports[7],.reset_pin=1,.sleep_port=&ports[8],.sleep_pin=1,.is_sleeping=true};
#define BACKUP_VALVE_EN_GPIO_Port (&ports[0])
#define N_FWD_EN_GPIO_Port (&ports[1])
#define N_BCKWD_EN_GPIO_Port (&ports[2])
#define P_FWD_EN_GPIO_Port (&ports[3])
#define P_BCKWD_EN_GPIO_Port (&ports[4])
#define STEPPER_SLEEP_GPIO_Port (&ports[8])
#define IGNITER_PIN_GPIO_Port (&ports[9])
#define NITROGEN_PIN_GPIO_Port (&ports[10])
#define NITROUS_PIN_GPIO_Port (&ports[11])
#define N2_SIG_GPIO_Port (&ports[12])
#define N20_SIG_GPIO_Port (&ports[13])
#define BACKUP_VALVE_EN_Pin 1U
#define N_FWD_EN_Pin 1U
#define N_BCKWD_EN_Pin 1U
#define P_FWD_EN_Pin 1U
#define P_BCKWD_EN_Pin 1U
#define STEPPER_SLEEP_Pin 1U
#define IGNITER_PIN_Pin 1U
#define NITROGEN_PIN_Pin 1U
#define NITROUS_PIN_Pin 1U
#define N2_SIG_Pin 1U
#define N20_SIG_Pin 1U
#define CMD_RETRACT_PLUMBING 8
#define PLUMBING_RETRACT_STEPS 20000000U
#define PLUMBING_RETRACT_STEP_PERIOD_US 30U
void HAL_GPIO_WritePin(GPIO_TypeDef*p,uint16_t pin,unsigned value){if(value)p->ODR|=pin;else{if((p==stepper.sleep_port&&stuck_sleep)||(p==stepper.step_port&&stuck_step))p->ODR|=pin;else p->ODR&=~pin;if(p==stepper.sleep_port)sleeps++;}}
void HAL_GPIO_Init(GPIO_TypeDef*p,GPIO_InitTypeDef*config){assert(p==stepper.step_port&&config->Pin==stepper.step_pin);}
void HAL_Delay(uint32_t ms){assert(ms==SLEEP_WAKE_MS&&!g_thread_comm_state_mutex.locked);wakes++;if(wake_interleave){void(*hook)(void)=wake_interleave;wake_interleave=0;hook();}}
void fake_timer_disable(TIM_HandleTypeDef*h){if(stuck_timer)h->Instance->CR1|=TIM_CR1_CEN;else h->Instance->CR1&=~TIM_CR1_CEN;}
void fake_clear_bits(uint32_t*reg,uint32_t bits){*reg&=~bits;if(reg==&fake_tim1.CR1&&stuck_timer)*reg|=TIM_CR1_CEN;}
int HAL_TIMEx_PWMN_Stop(TIM_HandleTypeDef*h,uint32_t channel){assert(h==&htim1&&channel==TIM_CHANNEL_3);if(stop_interleave){void(*hook)(void)=stop_interleave;stop_interleave=0;cleanup_stops++;hook();}if(fail_stop)return HAL_ERROR;fake_timer_disable(h);h->Instance->BDTR&=~TIM_BDTR_MOE;h->Instance->CCER&=~TIM_CCER_CC3NE;return HAL_OK;}
int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef*h,TIM_OC_InitTypeDef*c,uint32_t channel){assert(h==&htim1&&channel==TIM_CHANNEL_3&&c->Pulse==STEP_PULSE_US);return fail_config?HAL_ERROR:HAL_OK;}
int HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef*h,uint32_t channel){assert(h==&htim1&&channel==TIM_CHANNEL_3&&g_thread_comm_state_mutex.locked&&!g_thread_comm_abort&&!abort_latch);if(fail_start)return HAL_ERROR;pulses_started++;h->Instance->CCER|=TIM_CCER_CC3NE;h->Instance->CR1|=TIM_CR1_CEN;h->Instance->BDTR|=TIM_BDTR_MOE;return HAL_OK;}
static bool fill_settings_ready(void){return settings_ready;}
static uint8_t fill_settings_mode(void){return mode;}
static uint64_t fill_settings_cutoff(void){return settings_cutoff;}
static uint64_t telemetry_unix_ms(void){return network_now;}
static unsigned publish_umbilical_status(unsigned cmd,unsigned value){(void)cmd;(void)value;return 0;}
unsigned long tx_time_get(void){return tick;}
ACTUAL_OWNER
static FillMotionSnapshotV2 snapshot(void){FillMotionSnapshotV2 s;(void)main_task_fill_snapshot_v2(&s);return s;}
static void stop_in_wake(void){assert(thread_comm_request_manual_abort()==0);g_aborted=1;}
static unsigned sleeps_before_stale;
static void newer_in_wake(void){start_plumbing_retract(10001,thread_comm_manual_epoch());assert(pulses_started==1&&g_fill_provenance.source==10001);sleeps_before_stale=sleeps;}
static void recover(void){BoardRecoverySnapshot s;assert(board_recovery_snapshot(&s)==0);assert(board_recovery_begin_prepare(0,s.abort_epoch)==0);assert(board_recovery_apply_safe(0,s.abort_epoch,&s)==-14);board_recovery_execute_owned();assert(!thread_comm_get_abort());}
static void stop_recover_newer(void){stop_in_wake();recover();start_plumbing_retract(10002,thread_comm_manual_epoch());assert(stepperIsMoveActive(&stepper)&&g_fill_provenance.source==10002);sleeps_before_stale=sleeps;}
static void busy_in_wake(void){g_thread_comm_state_mutex.locked=1;}
#ifdef TEST_REAL_MUTEX
static pthread_t stop_tid;static bool stop_created;
static void*stop_thread_entry(void*unused){(void)unused;stop_worker=true;assert(thread_comm_request_manual_abort()==TX_SUCCESS);return 0;}
static void start_concurrent_stop(void){assert(pthread_create(&stop_tid,0,stop_thread_entry,0)==0);stop_created=true;while(!atomic_load(&stop_waiting))sched_yield();}
#endif
int main(int argc,char**argv){
 assert(argc==2);TX_BYTE_POOL pool={0};assert(thread_comm_init(&pool)==0);board_recovery_bind_owner(recovery_read_outputs,recovery_prepare_outputs,recovery_finish_owner);tick=100;mode=FILL_MODE_STEPPER;
 if(!strcmp(argv[1],"stop_wake")){wake_interleave=stop_in_wake;start_plumbing_retract(10000,thread_comm_manual_epoch());assert(wakes==1&&sleeps==1&&pulses_started==0&&!stepperIsMoveActive(&stepper)&&!(ports[8].ODR&1));assert(snapshot().lifecycle!=FILL_PROVENANCE_COMPLETED);}
 else if(!strcmp(argv[1],"newer_wake")||!strcmp(argv[1],"stop_recover_newer")){wake_interleave=!strcmp(argv[1],"newer_wake")?newer_in_wake:stop_recover_newer;start_plumbing_retract(10000,thread_comm_manual_epoch());assert(pulses_started==1&&wakes==2&&sleeps==sleeps_before_stale&&stepperIsMoveActive(&stepper));FillMotionSnapshotV2 s=snapshot();assert(s.busy&&s.lifecycle==FILL_PROVENANCE_ACTIVE&&s.accepted_source>10000);}
 else if(!strcmp(argv[1],"config_failure")||!strcmp(argv[1],"timer_failure")||!strcmp(argv[1],"failure_recovery")||!strcmp(argv[1],"config_recovery")){fail_config=!strcmp(argv[1],"config_failure")||!strcmp(argv[1],"config_recovery");fail_start=!fail_config;start_plumbing_retract(10000,thread_comm_manual_epoch());FillMotionSnapshotV2 s=snapshot();assert(s.fault&&s.lifecycle==FILL_PROVENANCE_FAULT&&!pulses_started&&!stepperIsMoveActive(&stepper)&&!(fake_tim1.DIER&TIM_IT_UPDATE));if(!strcmp(argv[1],"failure_recovery")||!strcmp(argv[1],"config_recovery")){stop_in_wake();recover();fprintf(stderr,"afterRecovery abort=%u fault=%u lifecycle=%u sleepODR=%u timerCR1=%u active=%u epoch=%u\n",thread_comm_get_abort(),g_fill_drive_fault,snapshot().lifecycle,ports[8].ODR,fake_tim1.CR1,stepperIsMoveActive(&stepper),snapshot().current_epoch);fail_start=false;fail_config=false;network_now=10001;start_plumbing_retract(10001,thread_comm_manual_epoch());assert(pulses_started==1&&snapshot().lifecycle==FILL_PROVENANCE_ACTIVE);}}
 else if(!strcmp(argv[1],"failure_unsafe")||!strcmp(argv[1],"step_stuck")||!strcmp(argv[1],"timer_stuck")||!strcmp(argv[1],"stop_failure")){
 fail_start=true;start_plumbing_retract(10000,thread_comm_manual_epoch());assert(snapshot().fault);stop_in_wake();stuck_sleep=!strcmp(argv[1],"failure_unsafe");stuck_step=!strcmp(argv[1],"step_stuck");stuck_timer=!strcmp(argv[1],"timer_stuck");fail_stop=!strcmp(argv[1],"stop_failure");if(stuck_step)ports[5].ODR=1;if(stuck_timer)fake_tim1.CR1|=TIM_CR1_CEN;
 BoardRecoverySnapshot b;assert(board_recovery_snapshot(&b)==0);uint32_t before=b.abort_epoch;
 assert(board_recovery_begin_prepare(0,before)==0);assert(board_recovery_apply_safe(0,before,&b)==-14);board_recovery_execute_owned();
 assert(thread_comm_get_abort()&&abort_latch&&g_fill_drive_fault&&!pulses_started);assert(board_recovery_snapshot(&b)==0&&b.abort_epoch==before);
 }
 else if(!strcmp(argv[1],"unknown_selector")||!strcmp(argv[1],"unready_selector")||!strcmp(argv[1],"exhausted_serial")){
 fail_start=true;start_plumbing_retract(10000,thread_comm_manual_epoch());stop_in_wake();if(!strcmp(argv[1],"unknown_selector"))mode=FILL_MODE_UNKNOWN;else if(!strcmp(argv[1],"unready_selector"))settings_ready=false;else g_fill_start_serial=UINT32_MAX;
 recover();assert(g_fill_drive_fault&&snapshot().lifecycle==FILL_PROVENANCE_FAULT&&!pulses_started);
 }
 else if(!strcmp(argv[1],"bootstrap_direct")){
 fail_start=true;start_plumbing_retract(10000,thread_comm_manual_epoch());assert(g_fill_drive_fault);g_thread_comm_state_mutex.locked=1;assert(bootstrap_stop_owner());g_thread_comm_state_mutex.locked=0;
 assert(g_fill_drive_fault&&g_aborted&&!pulses_started&&snapshot().lifecycle==FILL_PROVENANCE_FAULT&&g_fill_provenance.lifecycle!=FILL_PROVENANCE_COMPLETED);
 }

#ifdef TEST_REAL_MUTEX
 else if(!strcmp(argv[1],"concurrent_stop")){
 fail_start=true;start_plumbing_retract(10000,thread_comm_manual_epoch());stop_in_wake();BoardRecoverySnapshot b;assert(board_recovery_snapshot(&b)==0);uint32_t before=b.abort_epoch;
 assert(board_recovery_begin_prepare(0,before)==0);assert(board_recovery_apply_safe(0,before,&b)==-14);stop_interleave=start_concurrent_stop;board_recovery_execute_owned();
 assert(stop_created&&cleanup_stops==1);assert(pthread_join(stop_tid,0)==0);assert(thread_comm_get_abort()&&abort_latch&&!pulses_started);assert(board_recovery_snapshot(&b)==0&&b.abort_epoch>before);
 fail_start=false;start_plumbing_retract(10001,thread_comm_manual_epoch());assert(!pulses_started&&!(ports[8].ODR&1)&&!(fake_tim1.CR1&TIM_CR1_CEN)&&snapshot().lifecycle!=FILL_PROVENANCE_COMPLETED);
 }

#endif
 else if(!strcmp(argv[1],"postwake_busy")){wake_interleave=busy_in_wake;start_plumbing_retract(10000,thread_comm_manual_epoch());assert(!pulses_started&&!stepperIsMoveActive(&stepper));FillMotionSnapshotV2 s=snapshot();assert(s.mode==FILL_MODE_UNKNOWN&&s.minimum_remaining_ms==45000);g_thread_comm_state_mutex.locked=0;s=snapshot();assert(!s.busy&&s.lifecycle==FILL_PROVENANCE_UNKNOWN&&s.minimum_remaining_ms==45000);stop_in_wake();recover();assert(!(ports[8].ODR&1));}
 else if(!strcmp(argv[1],"minimum_and_complete")){start_selected_plumbing_retract(10000,thread_comm_manual_epoch());assert(pulses_started==1&&fake_tim1.ARR==29&&snapshot().minimum_remaining_ms==45000);tick=45101;assert(snapshot().minimum_remaining_ms==0&&snapshot().busy);for(unsigned i=0;i<PLUMBING_RETRACT_STEPS;i++)stepperTimerPeriodElapsedCallback(&htim1);assert(!stepperIsMoveActive(&stepper));service_plumbing_retract();assert(!snapshot().busy&&snapshot().lifecycle==FILL_PROVENANCE_COMPLETED&&!(ports[8].ODR&1));}
 else assert(0);
 (void)service_auxiliary_retract;(void)invalidate_motion_provenance_locked;
 assert(!ports[0].ODR&&!ports[1].ODR&&!ports[2].ODR&&!ports[3].ODR&&!ports[4].ODR);return 0;
}
