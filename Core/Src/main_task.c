#include "persistent_store_health.h"
#include "board_watchdog.h"
#include "AB-Threads.h"
#include "tx_api.h"
#include "thread_comm.h"
#include "board_recovery.h"
#include "board_bootstrap_storage.h"
#include "main.h"
#include "telemetry.h"
#include "status_report_retry.h"
#include "igniter_driver.h"
#include "solenoid_driver.h"
#include "stepper_driver.h"
#include "fill_motion.h"
#include "fill_motion_status.h"
#include "fill_settings.h"
#include "fill_owner_gate.h"

#include <stdint.h>
#include <stdatomic.h>

TX_THREAD main_task_thread;
/* Immediate state publication executes the Rust router on this task stack. */
#define MAIN_TASK_STACK_SIZE (12U * 1024U)
volatile uint32_t g_main_task_stack_remaining = MAIN_TASK_STACK_SIZE;
volatile uint32_t g_main_task_loop_count;

static void sample_main_task_stack(void)
{
    const volatile uint32_t *start = main_task_thread.tx_thread_stack_start;
    const volatile uint32_t *end = main_task_thread.tx_thread_stack_end;
    if (start == NULL || end == NULL || start >= end) return;
    const volatile uint32_t *p = start;
    while (p < end && *p == 0xEFEFEFEFUL) ++p;
    uint32_t remaining = (uint32_t)((uintptr_t)p - (uintptr_t)start);
    if (remaining < g_main_task_stack_remaining)
        g_main_task_stack_remaining = remaining;
}
/* GSE requires actual valve-state reports fresher than two seconds. */
#define UMBILICAL_STATUS_PERIOD_TICKS TX_TIMER_TICKS_PER_SECOND
#define LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS 5000U
#define LAUNCH_SEQUENCE_IGNITER_LATENCY_COMPENSATION_MS 750U
_Static_assert(THREAD_COMM_LAUNCH_EFFECT_DELAY_MS == LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS-LAUNCH_SEQUENCE_IGNITER_LATENCY_COMPENSATION_MS,"Launch timeline delay drift");
#define LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS 10000U
#define LAUNCH_SEQUENCE_RESTART_HOLDOFF_MS 1500U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_AGE_MS 5000U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_FUTURE_MS 1000U
#define PLUMBING_RETRACT_MOTOR_STEPS 200000U
#define PLUMBING_RETRACT_GEARBOX_RATIO 100U
#define PLUMBING_RETRACT_STEPS (PLUMBING_RETRACT_MOTOR_STEPS * PLUMBING_RETRACT_GEARBOX_RATIO)
#define PLUMBING_RETRACT_STEP_PERIOD_US 30U

static uint8_t g_aborted = 0U;
static FillMotion g_fill_motion;
static bool g_fill_mode_status_pending;
static FillMotionProvenance g_fill_provenance;
static uint32_t g_fill_start_serial;
static _Atomic bool g_fill_drive_fault;
volatile uint32_t g_sim_last_valve_command = UINT32_MAX;
volatile uint64_t g_sim_last_valve_command_ms = 0;
static uint8_t g_igniter_on = 0U;
static uint8_t g_nitrogen_open = 0U;
static uint8_t g_nitrous_open = 0U;
static uint8_t g_plumbing_retracted = 0U;
static _Atomic uint8_t g_plumbing_retract_active = 0U;
static _Atomic uint8_t g_launch_sequence_active = 0U;
static uint32_t g_launch_manual_epoch;
static thread_comm_launch_owner_t g_launch_owner;
static uint8_t g_launch_sequence_igniter_started = 0U;
static _Atomic uint8_t g_launch_sequence_holdoff_active = 0U;
static uint32_t g_plumbing_retract_steps_remaining = 0U;
static ULONG g_launch_sequence_igniter_start_ticks = 0U;
static ULONG g_launch_sequence_igniter_stop_ticks = 0U;
static ULONG g_launch_sequence_holdoff_start_ticks = 0U;
igniter_t igniter = {IGNITER_PIN_GPIO_Port, IGNITER_PIN_Pin, IGNITER_FAULT_GPIO_Port, IGNITER_FAULT_Pin, 3};
solenoid_t n2_solenoid = {NITROGEN_PIN_GPIO_Port, NITROGEN_PIN_Pin, N2_SIG_GPIO_Port, N2_SIG_Pin, N2_FAULT_GPIO_Port, N2_FAULT_Pin, 2};
solenoid_t n20_solenoid = {NITROUS_PIN_GPIO_Port, NITROUS_PIN_Pin, N20_SIG_GPIO_Port, N20_SIG_Pin, N20_FAULT_GPIO_Port, N20_FAULT_Pin, 1};
stepper_t stepper = {STEPPER_CTRL_GPIO_Port, STEPPER_CTRL_Pin, STEPPER_DIR_GPIO_Port, STEPPER_DIR_Pin, STEPPER_RESET_GPIO_Port, STEPPER_RESET_Pin, STEPPER_SLEEP_GPIO_Port, STEPPER_SLEEP_Pin, false, STEP_CW};

static void publish_all_umbilical_statuses(void)
{
    (void)publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
    (void)publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
    (void)publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
    (void)publish_umbilical_status(CMD_RETRACT_PLUMBING, g_plumbing_retracted);
}

static void publish_expected_outputs(void)
{
    (void)thread_comm_set_expected_outputs(g_nitrous_open, g_nitrogen_open, g_igniter_on);
}

static void publish_umbilical_statuses_if_due(ULONG *last_status_ticks)
{
    ULONG now = tx_time_get();

    if ((ULONG)(now - *last_status_ticks) >= UMBILICAL_STATUS_PERIOD_TICKS)
    {
        publish_all_umbilical_statuses();
        *last_status_ticks = now;
    }
}

static ULONG ms_to_ticks(uint32_t ms)
{
    ULONG ticks = (ULONG)(((uint64_t)ms * (uint64_t)TX_TIMER_TICKS_PER_SECOND + 999ULL) / 1000ULL);
    return (ticks == 0U) ? 1U : ticks;
}

static uint8_t igniter_on(void){if(igniterOn(&igniter)!=0)return 0U;g_igniter_on=1U;return 1U;}

static void igniter_off(void){igniterOff(&igniter);g_igniter_on=0U;}

/* Every driver is deasserted independent of selected mode. No live bridge
 * diagonal exists in firmware while the supplied circuit is uncorrected. */
static void all_fill_outputs_off(void)
{
 HAL_GPIO_WritePin(BACKUP_VALVE_EN_GPIO_Port,BACKUP_VALVE_EN_Pin,GPIO_PIN_RESET);
 HAL_GPIO_WritePin(N_FWD_EN_GPIO_Port,N_FWD_EN_Pin,GPIO_PIN_RESET);
 HAL_GPIO_WritePin(N_BCKWD_EN_GPIO_Port,N_BCKWD_EN_Pin,GPIO_PIN_RESET);
 HAL_GPIO_WritePin(P_FWD_EN_GPIO_Port,P_FWD_EN_Pin,GPIO_PIN_RESET);
 HAL_GPIO_WritePin(P_BCKWD_EN_GPIO_Port,P_BCKWD_EN_Pin,GPIO_PIN_RESET);
}
static bool fill_outputs_are_off(void)
{
 return (BACKUP_VALVE_EN_GPIO_Port->ODR&BACKUP_VALVE_EN_Pin)==0U&&
  (N_FWD_EN_GPIO_Port->ODR&N_FWD_EN_Pin)==0U&&(N_BCKWD_EN_GPIO_Port->ODR&N_BCKWD_EN_Pin)==0U&&
  (P_FWD_EN_GPIO_Port->ODR&P_FWD_EN_Pin)==0U&&(P_BCKWD_EN_GPIO_Port->ODR&P_BCKWD_EN_Pin)==0U;
}
static void fill_stop_locked(bool blocked,void*context)
{
 (void)blocked;(void)context;fill_motion_provenance_cancel(&g_fill_provenance,false);fill_motion_stop(&g_fill_motion);all_fill_outputs_off();if(!fill_outputs_are_off())g_fill_drive_fault=true;
}
static uint32_t fill_monotonic_ms(void)
{
 return (uint32_t)(((uint64_t)tx_time_get()*1000U)/TX_TIMER_TICKS_PER_SECOND);
}
static uint32_t fill_minimum_start_ms(void)
{
 /* Next tick rounded upward prevents a sub-tick early45s readiness proof. */
 return (uint32_t)((((uint64_t)tx_time_get()+1U)*1000U+TX_TIMER_TICKS_PER_SECOND-1U)/TX_TIMER_TICKS_PER_SECOND);
}

/* Called under actual shared state owner. No SDK/ThreadX/storage/delay. */
static void manual_stop_apply_locked(void)
{
    g_launch_sequence_active=0U;g_launch_sequence_igniter_started=0U;
    g_launch_sequence_holdoff_active=0U;g_plumbing_retract_active=0U;
    g_plumbing_retract_steps_remaining=0U;
    HAL_GPIO_WritePin(IGNITER_PIN_GPIO_Port,IGNITER_PIN_Pin,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(NITROGEN_PIN_GPIO_Port,NITROGEN_PIN_Pin,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(N2_SIG_GPIO_Port,N2_SIG_Pin,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(NITROUS_PIN_GPIO_Port,NITROUS_PIN_Pin,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(N20_SIG_GPIO_Port,N20_SIG_Pin,GPIO_PIN_RESET);
    stepperStopAsleepImmediate(&stepper);
    fill_stop_locked(true,0);
    g_igniter_on=g_nitrogen_open=g_nitrous_open=0U;
}

void main_task_force_outputs_safe_off(void)
{
    g_launch_sequence_active = 0U;
    g_launch_sequence_igniter_started = 0U;
    g_launch_sequence_holdoff_active = 0U;
    g_plumbing_retract_active = 0U;
    g_plumbing_retract_steps_remaining = 0U;

    HAL_GPIO_WritePin(IGNITER_PIN_GPIO_Port, IGNITER_PIN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(NITROGEN_PIN_GPIO_Port, NITROGEN_PIN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(N2_SIG_GPIO_Port, N2_SIG_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(NITROUS_PIN_GPIO_Port, NITROUS_PIN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(N20_SIG_GPIO_Port, N20_SIG_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BACKUP_VALVE_EN_GPIO_Port, BACKUP_VALVE_EN_Pin, GPIO_PIN_RESET);
    stepperSleep(&stepper);
    if(!thread_comm_fill_visit(fill_stop_locked,0)) all_fill_outputs_off();

    g_igniter_on = 0U;
    g_nitrogen_open = 0U;
    g_nitrous_open = 0U;
    publish_expected_outputs();
}

typedef struct {uint64_t source,now;uint32_t epoch,serial;uint8_t result;bool ticket,woke;} FillStart;
static bool next_fill_serial(uint32_t*out){if(g_fill_start_serial==UINT32_MAX){g_fill_drive_fault=true;fill_motion_provenance_cancel(&g_fill_provenance,true);return false;}*out=++g_fill_start_serial;return true;}
static void prepare_legacy_start(bool blocked,uint32_t epoch,void*context){
 FillStart*q=context;
 if(blocked||q->epoch!=epoch||g_fill_drive_fault||!fill_settings_ready()||fill_settings_mode()!=FILL_MODE_STEPPER||(fill_settings_cutoff()!=0U&&q->source<=fill_settings_cutoff()))return;
 if(g_plumbing_retract_active||g_fill_motion.active){q->result=FILL_MOTION_BUSY;return;}
 if(q->source&&g_fill_provenance.source&&q->source<=g_fill_provenance.source){q->result=q->source==g_fill_provenance.source?FILL_MOTION_DUPLICATE:FILL_MOTION_STALE;return;}
 if(!next_fill_serial(&q->serial))return;
 q->epoch=epoch;q->ticket=true;
}
static void wake_legacy_start(bool blocked,uint32_t epoch,void*context){
 FillStart*q=context;
 if(blocked||!q->ticket||q->serial!=g_fill_start_serial||epoch!=q->epoch)return;
 if(g_fill_drive_fault||!fill_settings_ready()||fill_settings_mode()!=FILL_MODE_STEPPER)return;
 q->woke=stepperWakeBegin(&stepper)==STEP_OK;
}
static void wake_initial_holding(void*context){
 bool*woke=context;*woke=stepperWakeBegin(&stepper)==STEP_OK;
}
static void finish_legacy_start(bool blocked,uint32_t epoch,void*context){
 FillStart*q=context;
 /* Cleanup belongs to this ticket only; never disable a newer accepted move. */
 if(!q->ticket||q->serial!=g_fill_start_serial)return;
 if(blocked||epoch!=q->epoch||!fill_settings_ready()||fill_settings_mode()!=FILL_MODE_STEPPER||(fill_settings_cutoff()!=0U&&q->source<=fill_settings_cutoff())){
  if(!g_plumbing_retract_active&&!g_fill_motion.active){stepperSleep(&stepper);all_fill_outputs_off();fill_motion_provenance_cancel(&g_fill_provenance,false);}
  q->result=FILL_MOTION_STALE;return;
 }
 if(!q->woke){g_fill_drive_fault=true;fill_motion_provenance_cancel(&g_fill_provenance,true);q->result=FILL_MOTION_FAULT;return;}
 if(stepperStartMoveTimer(&stepper,PLUMBING_RETRACT_STEPS,PLUMBING_RETRACT_STEP_PERIOD_US)==STEP_OK){
  g_plumbing_retract_steps_remaining=PLUMBING_RETRACT_STEPS;g_plumbing_retract_active=1U;
  fill_motion_provenance_start(&g_fill_provenance,q->source,epoch,FILL_ACTION_RETRACT,fill_minimum_start_ms());q->result=FILL_MOTION_STARTED;
 }else{g_fill_drive_fault=true;fill_motion_provenance_cancel(&g_fill_provenance,true);q->result=FILL_MOTION_FAULT;}
}
static void start_plumbing_retract(uint64_t source,uint32_t command_epoch){
 FillStart request={.source=source,.epoch=command_epoch,.result=FILL_MOTION_FAULT};
 if(!thread_comm_fill_visit_epoch(source,prepare_legacy_start,&request)||!request.ticket)return;
 /* Preserve the legacy report; V2 alone proves actual timer acceptance. */
 g_plumbing_retracted=1U;(void)publish_umbilical_status(CMD_RETRACT_PLUMBING,g_plumbing_retracted);
 stepperSetDir(&stepper,STEP_CW);
 (void)thread_comm_fill_visit_epoch(source,wake_legacy_start,&request);
 if(request.woke)request.woke=stepperWakeWait(&stepper)==STEP_OK;
 (void)thread_comm_fill_visit_epoch(source,finish_legacy_start,&request);
}
static void start_aux_locked(bool blocked,uint32_t epoch,void*context)
{
 FillStart*request=context;
 if(blocked||request->epoch!=epoch||g_fill_drive_fault||!fill_settings_ready()||request->source<=fill_settings_cutoff())return;
 if(g_fill_start_serial==UINT32_MAX){g_fill_drive_fault=true;fill_motion_provenance_cancel(&g_fill_provenance,true);return;}
 request->result=fill_motion_start(&g_fill_motion,fill_settings_mode(),FILL_ACTION_RETRACT,request->source,request->now,fill_monotonic_ms());
 if(request->result==FILL_MOTION_STARTED){
  g_plumbing_retracted=0U;HAL_GPIO_WritePin(BACKUP_VALVE_EN_GPIO_Port,BACKUP_VALVE_EN_Pin,GPIO_PIN_SET);
  if((BACKUP_VALVE_EN_GPIO_Port->ODR&BACKUP_VALVE_EN_Pin)==0U){fill_stop_locked(true,0);g_fill_drive_fault=true;fill_motion_provenance_cancel(&g_fill_provenance,true);request->result=FILL_MOTION_FAULT;}
  else{(void)next_fill_serial(&request->serial);fill_motion_provenance_start(&g_fill_provenance,request->source,epoch,FILL_ACTION_RETRACT,fill_minimum_start_ms());}
 }
}
static void start_selected_plumbing_retract(uint64_t source,uint32_t command_epoch)
{
 if(!fill_settings_ready())return;
 if(fill_settings_mode()==FILL_MODE_STEPPER){
  if(fill_settings_cutoff()!=0U&&source<=fill_settings_cutoff())return;
  start_plumbing_retract(source,command_epoch);return;
 }
 uint64_t now=0U;
#ifdef TELEMETRY_ENABLED
 now=telemetry_unix_ms();
#endif
 FillStart request={.source=source,.epoch=command_epoch,.now=now,.result=FILL_MOTION_UNSUPPORTED};
 (void)thread_comm_fill_visit_epoch(source,start_aux_locked,&request);
 /* No early completed status. Repeated active requests cannot extend45s. */
 if(request.result==FILL_MOTION_STARTED)(void)publish_umbilical_status(CMD_RETRACT_PLUMBING,0U);
}
static void service_aux_locked(bool blocked,void*context)
{
 bool*completed=context;
 if(blocked||!fill_settings_ready()){fill_stop_locked(true,0);return;}
 if(fill_motion_service(&g_fill_motion,fill_monotonic_ms())){
  all_fill_outputs_off();
  if(fill_outputs_are_off()){g_plumbing_retracted=1U;fill_motion_provenance_complete(&g_fill_provenance);(void)fill_motion_provenance_remaining(&g_fill_provenance,fill_monotonic_ms());*completed=true;}
  else{g_fill_motion.completed=false;g_fill_drive_fault=true;fill_motion_provenance_cancel(&g_fill_provenance,true);}
 }
}
static void service_auxiliary_retract(void)
{
 bool completed=false;
 if(!thread_comm_fill_visit(service_aux_locked,&completed))all_fill_outputs_off();
 if(completed)(void)publish_umbilical_status(CMD_RETRACT_PLUMBING,1U);
}
static void service_legacy_locked(bool blocked,uint32_t epoch,void*context){
 (void)context;
 if(!g_plumbing_retract_active)return;
 if(blocked||epoch!=g_fill_provenance.origin_epoch){stepperSleep(&stepper);g_plumbing_retract_active=0U;g_plumbing_retract_steps_remaining=0U;fill_motion_provenance_cancel(&g_fill_provenance,false);return;}
 if(!stepperIsMoveActive(&stepper)){
  /* Existing legacy completion policy; no new45s torque release is introduced. */
  stepperSleep(&stepper);g_plumbing_retract_active=0U;g_plumbing_retract_steps_remaining=0U;fill_motion_provenance_complete(&g_fill_provenance);
 }
 (void)fill_motion_provenance_remaining(&g_fill_provenance,fill_monotonic_ms());
}
static void service_plumbing_retract(void){
 /* Transient owner refusal retains busy; a later current owner retries service. */
 (void)thread_comm_fill_visit_epoch(0U,service_legacy_locked,0);
}

typedef struct{uint32_t start;uint32_t epoch;} LaunchStart;
static void start_launch_sequence_locked(void*context){
 LaunchStart*q=context;if(g_launch_sequence_active||g_launch_sequence_holdoff_active)return;
 g_launch_manual_epoch=q->epoch;g_launch_sequence_igniter_start_ticks=q->start;
 g_launch_sequence_igniter_stop_ticks=q->start+ms_to_ticks(LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS);
 g_launch_sequence_igniter_started=0U;g_launch_sequence_active=1U;
}
static void start_launch_sequence(uint64_t source,uint32_t epoch){
 thread_comm_launch_owner_t owner={0};
 if(!thread_comm_launch_claim(source,epoch,&owner))return;
 LaunchStart request={(uint32_t)tx_time_get()+(uint32_t)ms_to_ticks(THREAD_COMM_LAUNCH_EFFECT_DELAY_MS),owner.epoch};
 g_launch_owner=owner;
 (void)thread_comm_launch_begin_owned_deadline(&owner,start_launch_sequence_locked,&request,&request.start);
}

static void service_launch_sequence_locked(void*context)
{
    (void)context;
    if (g_launch_sequence_active != 0U)
    {
        const ULONG now = tx_time_get();

        if ((g_launch_sequence_igniter_started == 0U) &&
            ((ULONG)(now - g_launch_sequence_igniter_start_ticks) < (ULONG)(1UL << 31)))
        {
            if (igniter_on() != 0U)
            {
                g_launch_sequence_igniter_started = 1U;
                g_launch_sequence_igniter_stop_ticks =
                    now + ms_to_ticks(LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS);
            }
        }

        if ((g_launch_sequence_igniter_started != 0U) &&
            ((ULONG)(now - g_launch_sequence_igniter_stop_ticks) < (ULONG)(1UL << 31)))
        {
            igniter_off();
            g_launch_sequence_active = 0U;
            g_launch_sequence_igniter_started = 0U;
            g_launch_sequence_holdoff_active = 1U;
            g_launch_sequence_holdoff_start_ticks = tx_time_get();
        }
    }

    if ((g_launch_sequence_holdoff_active != 0U) &&
        ((ULONG)(tx_time_get() - g_launch_sequence_holdoff_start_ticks) >=
         ms_to_ticks(LAUNCH_SEQUENCE_RESTART_HOLDOFF_MS)))
    {
        g_launch_sequence_holdoff_active = 0U;
    }
}
static void service_launch_sequence(void){
 uint8_t previous=g_igniter_on;
 (void)thread_comm_launch_effect_owned(&g_launch_owner,service_launch_sequence_locked,0);
 if(!g_launch_sequence_active&&!g_launch_sequence_holdoff_active)thread_comm_launch_complete_owned(&g_launch_owner);
 if(previous!=g_igniter_on){(void)publish_umbilical_status(CMD_IGNITER_ON,g_igniter_on);publish_expected_outputs();}
}

/* Only bounded driver GPIO accesses and local state updates execute under the
 * launch-state mutex. SDK publication and expected-output snapshots follow
 * after unlock, so network admission cannot block launch serialization. */
static void apply_automatic_valve(uint8_t command)
{
    switch (command) {
    case CMD_NITROGEN_OPEN:
        if (solenoidOn(&n2_solenoid) == 0) g_nitrogen_open = 1U;
        break;
    case CMD_NITROGEN_CLOSE:
        solenoidOff(&n2_solenoid); g_nitrogen_open = 0U; break;
    case CMD_NITROUS_OPEN:
        if (solenoidOn(&n20_solenoid) == 0) g_nitrous_open = 1U;
        break;
    case CMD_NITROUS_CLOSE:
        solenoidOff(&n20_solenoid); g_nitrous_open = 0U; break;
    default: break;
    }
}

static void apply_manual_valve_locked(void*context){
 uint8_t command=*(const uint8_t*)context;
 if(command==CMD_IGNITER_ON||command==CMD_IGNITER_OFF){
  g_launch_sequence_active=0U;g_launch_sequence_igniter_started=0U;g_launch_sequence_holdoff_active=0U;
  if(command==CMD_IGNITER_ON)(void)igniter_on();else igniter_off();
 }else apply_automatic_valve(command);
}
static void invalidate_motion_provenance_locked(bool blocked,void*context);
static void handle_command(thread_comm_msg_t cmd,uint32_t command_epoch)
{
    /* The narrow receipt path owns an already accepted original Launch.
     * No new generic command/storage readiness exception is created. */
    if(cmd.cmd==CMD_IGNITER_SEQUENCE){
        start_launch_sequence(cmd.timestamp_ms,command_epoch);return;
    }
    if (!persistent_store_controls_ready()) {
        if(!thread_comm_launch_continuity())main_task_force_outputs_safe_off();
        return;
    }

    if (thread_comm_get_abort() != 0U)
    {
        main_task_force_outputs_safe_off();
        return;
    }

    if (!thread_comm_command_timestamp_current(cmd.timestamp_ms)) return;

    if ((cmd.cmd & THREAD_COMM_AUTOMATIC_TAG) != 0U) {
        if (thread_comm_apply_automatic_owned(cmd,command_epoch,apply_automatic_valve) != 0U) {
            const uint8_t command = cmd.cmd & (uint8_t)~THREAD_COMM_AUTOMATIC_TAG;
            g_sim_last_valve_command = command;
            g_sim_last_valve_command_ms = ((uint64_t)tx_time_get() * 1000ULL) / TX_TIMER_TICKS_PER_SECOND;
            publish_expected_outputs();
            if (command == CMD_NITROGEN_OPEN || command == CMD_NITROGEN_CLOSE)
                (void)publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
            else
                (void)publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
        }
        return;
    }

    g_sim_last_valve_command = (uint32_t)cmd.cmd;
    g_sim_last_valve_command_ms = ((uint64_t)tx_time_get() * 1000ULL) /
                                  TX_TIMER_TICKS_PER_SECOND;
    if(cmd.cmd==CMD_IGNITER_ON||cmd.cmd==CMD_IGNITER_OFF||cmd.cmd==CMD_NITROGEN_OPEN||cmd.cmd==CMD_NITROGEN_CLOSE||cmd.cmd==CMD_NITROUS_OPEN||cmd.cmd==CMD_NITROUS_CLOSE){
      if(thread_comm_manual_effect_owned(cmd.timestamp_ms,command_epoch,apply_manual_valve_locked,&cmd.cmd)){
        if(cmd.cmd==CMD_IGNITER_ON||cmd.cmd==CMD_IGNITER_OFF)(void)publish_umbilical_status(CMD_IGNITER_ON,g_igniter_on);
        else if(cmd.cmd==CMD_NITROGEN_OPEN||cmd.cmd==CMD_NITROGEN_CLOSE)(void)publish_umbilical_status(CMD_NITROGEN_OPEN,g_nitrogen_open);
        else (void)publish_umbilical_status(CMD_NITROUS_OPEN,g_nitrous_open);
        publish_expected_outputs();
      }
      return;
    }
    switch(cmd.cmd){
    case CMD_RETRACT_PLUMBING:start_selected_plumbing_retract(cmd.timestamp_ms,command_epoch);break;
    case CMD_EXTEND_PLUMBING:(void)thread_comm_fill_visit(invalidate_motion_provenance_locked,0);break;
    case CMD_IGNITER_SEQUENCE:start_launch_sequence(cmd.timestamp_ms,command_epoch);break;
    default:break;
    }
}


/* Register readback is commanded electrical state, never valve-position proof. */
static uint8_t recovery_read_outputs(uint32_t *mask)
{
    uint32_t value = 0U;
    if ((IGNITER_PIN_GPIO_Port->ODR & IGNITER_PIN_Pin) != 0U) value |= 1U;
    if ((NITROGEN_PIN_GPIO_Port->ODR & NITROGEN_PIN_Pin) != 0U) value |= 2U;
    if ((NITROUS_PIN_GPIO_Port->ODR & NITROUS_PIN_Pin) != 0U) value |= 4U;
    if ((BACKUP_VALVE_EN_GPIO_Port->ODR & BACKUP_VALVE_EN_Pin) != 0U) value |= 8U;
    if ((STEPPER_SLEEP_GPIO_Port->ODR & STEPPER_SLEEP_Pin) != 0U) value |= 16U;
    if ((N2_SIG_GPIO_Port->ODR & N2_SIG_Pin) != 0U) value |= 32U;
    if ((N20_SIG_GPIO_Port->ODR & N20_SIG_Pin) != 0U) value |= 64U;
    if ((N_FWD_EN_GPIO_Port->ODR & N_FWD_EN_Pin) != 0U) value |= 128U;
    if ((N_BCKWD_EN_GPIO_Port->ODR & N_BCKWD_EN_Pin) != 0U) value |= 256U;
    if ((P_FWD_EN_GPIO_Port->ODR & P_FWD_EN_Pin) != 0U) value |= 512U;
    if ((P_BCKWD_EN_GPIO_Port->ODR & P_BCKWD_EN_Pin) != 0U) value |= 1024U;
    if(g_fill_motion.active) value |= 2048U;
    *mask = value;
    return BOARD_RECOVERY_OUTPUTS_VALID | (value == 0U ? BOARD_RECOVERY_SAFE_OUTPUTS : 0U) |
        (g_aborted != 0U && g_launch_sequence_active == 0U ? BOARD_RECOVERY_OWNER_STOPPED : 0U);
}
static uint8_t recovery_prepare_base(uint8_t desired)
{
    if (desired == 0U) {
        HAL_GPIO_WritePin(IGNITER_PIN_GPIO_Port, IGNITER_PIN_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(NITROGEN_PIN_GPIO_Port, NITROGEN_PIN_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(N2_SIG_GPIO_Port, N2_SIG_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(NITROUS_PIN_GPIO_Port, NITROUS_PIN_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(N20_SIG_GPIO_Port, N20_SIG_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BACKUP_VALVE_EN_GPIO_Port, BACKUP_VALVE_EN_Pin, GPIO_PIN_RESET);
        stepperSleep(&stepper);
        fill_stop_locked(true,0);
    }
    uint32_t mask;
    return (recovery_read_outputs(&mask) & BOARD_RECOVERY_SAFE_OUTPUTS) != 0U;
}
static void recovery_finish_base(void)
{
    g_aborted = 0U;
    g_launch_sequence_active = 0U;
    g_launch_sequence_igniter_started = 0U;
    g_launch_sequence_holdoff_active = 0U;
    g_plumbing_retract_active = 0U;
    g_plumbing_retract_steps_remaining = 0U;
    g_igniter_on = g_nitrogen_open = g_nitrous_open = 0U;
}

/* These registered callbacks are ordinary recovery only. Bootstrap uses
 * the base cleanup directly and must never rearm a plumbing fault. */
static uint8_t recovery_prepare_outputs(uint8_t desired)
{
    if (!recovery_prepare_base(desired) || stepperRecoverAsleep(&stepper) != STEP_OK)
        return 0U;
    uint32_t mask = UINT32_MAX;
    const uint8_t required = BOARD_RECOVERY_OUTPUTS_VALID | BOARD_RECOVERY_SAFE_OUTPUTS;
    return (recovery_read_outputs(&mask) & required) == required && mask == 0U &&
        stepperIsStoppedAsleep(&stepper);
}
static bool recovery_fill_rearm_ready(void)
{
    const uint8_t mode = fill_settings_mode();
    uint32_t mask = UINT32_MAX;
    const uint8_t required = BOARD_RECOVERY_OUTPUTS_VALID | BOARD_RECOVERY_SAFE_OUTPUTS;
    return fill_settings_ready() && (mode == FILL_MODE_STEPPER || mode == FILL_MODE_AUX) &&
        g_fill_start_serial != UINT32_MAX && !g_fill_motion.active &&
        !g_plumbing_retract_active && stepperIsStoppedAsleep(&stepper) &&
        (recovery_read_outputs(&mask) & required) == required && mask == 0U;
}
static void recovery_finish_owner(void)
{
    recovery_finish_base();
    if (recovery_fill_rearm_ready()) g_fill_drive_fault = false;
    /* Prior source/action/epoch remains cancelled; only a new accepted move
     * can establish a new minimum45 token or a completed motion. */
}

/* Called under state owner after protocol/generation/epoch/freshness checks.
 * Only an explicit legacy->aux switch may release idle holding torque. */
uint8_t main_task_fill_prepare_mode(uint8_t mode)
{
 if(g_plumbing_retract_active||g_fill_motion.active||stepperIsMoveActive(&stepper)||
    g_launch_sequence_active||g_launch_sequence_holdoff_active)return 0U;
 uint32_t mask=UINT32_MAX;
 if((recovery_read_outputs(&mask)&BOARD_RECOVERY_OUTPUTS_VALID)==0U)return 0U;
 bool release=fill_settings_mode()==FILL_MODE_STEPPER&&mode==FILL_MODE_AUX;
 if((mask&~UINT32_C(16))!=0U||((mask&16U)!=0U&&!release))return 0U;
 if(release&&(mask&16U)!=0U)stepperSleep(&stepper);
 return (recovery_read_outputs(&mask)&BOARD_RECOVERY_SAFE_OUTPUTS)!=0U;
}
void main_task_fill_mode_changed(void)
{
 g_plumbing_retracted=0U;fill_motion_provenance_cancel(&g_fill_provenance,false);fill_motion_stop(&g_fill_motion);all_fill_outputs_off();g_fill_drive_fault=!fill_outputs_are_off();g_fill_mode_status_pending=true;
}
typedef struct {uint8_t mode,action,busy;uint32_t remaining;bool fault;} FillSnapshot;
static void fill_snapshot_locked(bool blocked,void*context)
{
 (void)blocked;FillSnapshot*out=context;out->fault=g_fill_drive_fault;out->mode=fill_settings_ready()?fill_settings_mode():FILL_MODE_UNKNOWN;
 out->busy=g_fill_motion.active||g_plumbing_retract_active;
 out->action=out->busy?FILL_ACTION_RETRACT:FILL_ACTION_NONE;
 out->remaining=g_fill_motion.active?fill_motion_remaining(&g_fill_motion,fill_monotonic_ms()):0U;
}
static void invalidate_motion_provenance_locked(bool blocked,void*context){
 (void)blocked;(void)context;fill_motion_provenance_cancel(&g_fill_provenance,false);
}
static void fill_snapshot_v2_locked(bool blocked,uint32_t epoch,void*context){
 FillMotionSnapshotV2*out=context;out->mode=fill_settings_ready()?fill_settings_mode():FILL_MODE_UNKNOWN;out->current_epoch=epoch;
 out->busy=g_fill_motion.active||g_plumbing_retract_active;out->active_action=out->busy?FILL_ACTION_RETRACT:FILL_ACTION_NONE;
 out->remaining_ms=g_fill_motion.active?fill_motion_remaining(&g_fill_motion,fill_monotonic_ms()):0U;
 out->accepted_source=g_fill_provenance.source;out->accepted_action=g_fill_provenance.action;out->origin_epoch=g_fill_provenance.origin_epoch;out->lifecycle=g_fill_provenance.lifecycle;
 out->minimum_remaining_ms=fill_motion_provenance_remaining(&g_fill_provenance,fill_monotonic_ms());out->fault=g_fill_drive_fault;
 if(out->fault)out->lifecycle=FILL_PROVENANCE_FAULT;
 else if(blocked||out->mode==FILL_MODE_UNKNOWN||out->origin_epoch!=epoch){if(out->lifecycle!=FILL_PROVENANCE_UNKNOWN)out->lifecycle=FILL_PROVENANCE_CANCELLED;}
}
bool main_task_fill_snapshot_v2(FillMotionSnapshotV2*out){
 if(!out)return false;
 *out=(FillMotionSnapshotV2){.mode=FILL_MODE_UNKNOWN,.minimum_remaining_ms=FILL_DURATION_MS};
 return thread_comm_fill_visit_epoch(0U,fill_snapshot_v2_locked,out)&&!out->fault;
}
bool main_task_fill_snapshot(uint8_t*mode,uint8_t*action,uint8_t*busy,uint32_t*remaining)
{
 FillSnapshot snapshot={.mode=FILL_MODE_UNKNOWN};bool valid=thread_comm_fill_visit(fill_snapshot_locked,&snapshot);
 *mode=snapshot.mode;*action=snapshot.action;*busy=snapshot.busy;*remaining=snapshot.remaining;return valid&&!snapshot.fault;
}
/* Executed only by the main control owner for an explicit staged first leg. */
static uint8_t bootstrap_stop_owner(void) {
 if(!recovery_prepare_base(0U))return 0U;
 recovery_finish_base();g_aborted=1U;return 1U;
}
void main_task_entry(ULONG initial_input)
{
    (void)initial_input;
    (void)thread_comm_bind_manual_stop_apply(manual_stop_apply_locked);
    solenoidInit(&n2_solenoid);
    solenoidInit(&n20_solenoid);
    igniterInit(&igniter);
    fill_settings_restore();
    if(persistent_store_controls_ready()&&fill_settings_ready()&&fill_settings_mode()==FILL_MODE_STEPPER) {
        if(stepperInitAsleep(&stepper)!=STEP_OK)g_fill_drive_fault=true;
        bool woke=false;
        if(!g_fill_drive_fault&&thread_comm_manual_effect(0U,wake_initial_holding,&woke)&&woke)
            (void)stepperWakeWait(&stepper); /* No GPIO enable after the delay. */
    } else if(stepperInitAsleep(&stepper)!=STEP_OK) {
        g_fill_drive_fault=true; /* Plumbing only; never create an automatic Abort. */
    }

    thread_comm_msg_t msg;uint32_t command_epoch=0U;
    ULONG last_umbilical_status_ticks = tx_time_get();

    board_recovery_bind_owner(recovery_read_outputs, recovery_prepare_outputs, recovery_finish_owner);
    (void)board_bootstrap_storage_bind_owner(bootstrap_stop_owner);
    publish_all_umbilical_statuses();
    publish_expected_outputs();

    for (;;)
    {
        board_watchdog_progress(BOARD_WATCHDOG_CONTROL);
        thread_comm_service_board_recovery();
        board_bootstrap_storage_service();
        fill_settings_service();
        if(g_fill_mode_status_pending){g_fill_mode_status_pending=false;(void)publish_umbilical_status(CMD_RETRACT_PLUMBING,0U);}
        if (!persistent_store_controls_ready()&&!thread_comm_launch_continuity()) main_task_force_outputs_safe_off();
        sample_main_task_stack();
        g_main_task_loop_count++;
        if (thread_comm_get_abort() != 0U)
        {
            main_task_force_outputs_safe_off();

            if (g_aborted == 0U)
            {
                (void)publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
                (void)publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
                (void)publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
                g_aborted = 1U;
            }

            while (thread_comm_receive_owned(&msg,&command_epoch) == TX_SUCCESS)
            {
                // Drop pending commands while abort is latched.
            }

            publish_umbilical_statuses_if_due(&last_umbilical_status_ticks);
            retry_pending_status_reports();
            tx_thread_sleep(10);
            continue;
        }

        while (thread_comm_receive_owned(&msg,&command_epoch) == TX_SUCCESS)
        {
            handle_command(msg,command_epoch);
        }

        service_launch_sequence();
        service_plumbing_retract();
        service_auxiliary_retract();
        publish_umbilical_statuses_if_due(&last_umbilical_status_ticks);
        retry_pending_status_reports();

        tx_thread_sleep(1);
    }
}

UINT create_main_task(TX_BYTE_POOL *byte_pool)
{
    CHAR *pointer;

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer,
                         MAIN_TASK_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
    {
        return TX_POOL_ERROR;
    }

    return tx_thread_create(&main_task_thread,
                            "Main Task",
                            main_task_entry,
                            2,
                            pointer,
                            MAIN_TASK_STACK_SIZE,
                            6,
                            6,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START);
}
