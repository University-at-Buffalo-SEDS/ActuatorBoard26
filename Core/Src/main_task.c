#include "AB-Threads.h"
#include "tx_api.h"
#include "thread_comm.h"
#include "main.h"
#include "telemetry.h"
#include "status_report_retry.h"
#include "igniter_driver.h"
#include "solenoid_driver.h"
#include "stepper_driver.h"

#include <stdint.h>

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
#define UMBILICAL_STATUS_PERIOD_TICKS (5U * TX_TIMER_TICKS_PER_SECOND)
#define LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS 5000U
#define LAUNCH_SEQUENCE_IGNITER_LATENCY_COMPENSATION_MS 750U
#define LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS 10000U
#define LAUNCH_SEQUENCE_RESTART_HOLDOFF_MS 1500U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_AGE_MS 5000U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_FUTURE_MS 1000U
#define PLUMBING_RETRACT_MOTOR_STEPS 200000U
#define PLUMBING_RETRACT_GEARBOX_RATIO 100U
#define PLUMBING_RETRACT_STEPS (PLUMBING_RETRACT_MOTOR_STEPS * PLUMBING_RETRACT_GEARBOX_RATIO)
#define PLUMBING_RETRACT_STEP_PERIOD_US 30U

static uint8_t g_aborted = 0U;
volatile uint32_t g_sim_last_valve_command = UINT32_MAX;
volatile uint64_t g_sim_last_valve_command_ms = 0;
static uint8_t g_igniter_on = 0U;
static uint8_t g_nitrogen_open = 0U;
static uint8_t g_nitrous_open = 0U;
static uint8_t g_plumbing_retracted = 0U;
static uint8_t g_plumbing_retract_active = 0U;
static uint8_t g_launch_sequence_active = 0U;
static uint8_t g_launch_sequence_igniter_started = 0U;
static uint8_t g_launch_sequence_holdoff_active = 0U;
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

static uint8_t igniter_on(void)
{
    if (igniterOn(&igniter) == 0)
    {
        const uint8_t was_on = g_igniter_on;
        g_igniter_on = 1U;
        if (was_on == 0U)
        {
            (void)publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
            publish_expected_outputs();
        }
        return 1U;
    }

    return 0U;
}

static void igniter_off(void)
{
    igniterOff(&igniter);
    g_igniter_on = 0U;
    (void)publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
    publish_expected_outputs();
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

    g_igniter_on = 0U;
    g_nitrogen_open = 0U;
    g_nitrous_open = 0U;
    publish_expected_outputs();
}

static void start_plumbing_retract(void)
{
    if (g_plumbing_retract_active != 0U)
    {
        return;
    }

    g_plumbing_retracted = 1U;
    (void)publish_umbilical_status(CMD_RETRACT_PLUMBING, g_plumbing_retracted);

    stepperSetDir(&stepper, STEP_CW);
    if (stepperWake(&stepper) != STEP_OK)
    {
        return;
    }

    if (stepperStartMoveTimer(&stepper, PLUMBING_RETRACT_STEPS, PLUMBING_RETRACT_STEP_PERIOD_US) == STEP_OK)
    {
        g_plumbing_retract_steps_remaining = PLUMBING_RETRACT_STEPS;
        g_plumbing_retract_active = 1U;
    }
}

static void service_plumbing_retract(void)
{
    if (g_plumbing_retract_active == 0U)
    {
        return;
    }

    if (thread_comm_get_abort() != 0U)
    {
        main_task_force_outputs_safe_off();
        return;
    }

    if (!stepperIsMoveActive(&stepper))
    {
        stepperSleep(&stepper);
        g_plumbing_retract_active = 0U;
        g_plumbing_retract_steps_remaining = 0U;
    }
}

static ULONG launch_sequence_igniter_start_ticks(uint64_t packet_timestamp_ms)
{
    const ULONG now_ticks = tx_time_get();
    const uint32_t compensated_start_delay_ms =
        (LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS > LAUNCH_SEQUENCE_IGNITER_LATENCY_COMPENSATION_MS)
            ? (LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS -
               LAUNCH_SEQUENCE_IGNITER_LATENCY_COMPENSATION_MS)
            : 0U;
    const ULONG start_delay_ticks = ms_to_ticks(compensated_start_delay_ms);

    if (packet_timestamp_ms == 0ULL)
    {
        return now_ticks + start_delay_ticks;
    }

    const uint64_t now_ms = telemetry_unix_ms();
    if (now_ms == 0ULL)
    {
        return now_ticks + start_delay_ticks;
    }

    if (now_ms >= packet_timestamp_ms)
    {
        const uint64_t packet_age_ms = now_ms - packet_timestamp_ms;
        if (packet_age_ms > LAUNCH_SEQUENCE_TIMESTAMP_MAX_AGE_MS)
        {
            return now_ticks + start_delay_ticks;
        }

        if (packet_age_ms >= compensated_start_delay_ms)
        {
            return now_ticks;
        }

        return now_ticks + ms_to_ticks((uint32_t)(compensated_start_delay_ms - packet_age_ms));
    }

    const uint64_t packet_future_ms = packet_timestamp_ms - now_ms;
    if (packet_future_ms > LAUNCH_SEQUENCE_TIMESTAMP_MAX_FUTURE_MS)
    {
        return now_ticks + start_delay_ticks;
    }

    return now_ticks + ms_to_ticks((uint32_t)(packet_future_ms + compensated_start_delay_ms));
}

static void start_launch_sequence(uint64_t packet_timestamp_ms)
{
    if ((g_launch_sequence_active != 0U) || (g_launch_sequence_holdoff_active != 0U))
    {
        return;
    }

    g_launch_sequence_igniter_start_ticks = launch_sequence_igniter_start_ticks(packet_timestamp_ms);
    g_launch_sequence_igniter_stop_ticks =
        g_launch_sequence_igniter_start_ticks + ms_to_ticks(LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS);
    g_launch_sequence_igniter_started = 0U;
    g_launch_sequence_active = 1U;
}

static void service_launch_sequence(void)
{
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

static void handle_command(thread_comm_msg_t cmd)
{
    if (thread_comm_get_abort() != 0U)
    {
        main_task_force_outputs_safe_off();
        return;
    }

    g_sim_last_valve_command = (uint32_t)cmd.cmd;
    g_sim_last_valve_command_ms = ((uint64_t)tx_time_get() * 1000ULL) /
                                  TX_TIMER_TICKS_PER_SECOND;
    switch (cmd.cmd){
    case CMD_IGNITER_ON:
        g_launch_sequence_active = 0U;
        g_launch_sequence_igniter_started = 0U;
        g_launch_sequence_holdoff_active = 0U;
        igniter_on();
        if (thread_comm_get_abort() != 0U)
        {
            main_task_force_outputs_safe_off();
        }
        break;

    case CMD_IGNITER_OFF:
        g_launch_sequence_active = 0U;
        g_launch_sequence_igniter_started = 0U;
        g_launch_sequence_holdoff_active = 0U;
        igniter_off();
        break;

    case CMD_NITROGEN_OPEN:
        if (solenoidOn(&n2_solenoid) == 0)
        {
            g_nitrogen_open = 1U;
            publish_expected_outputs();
        }
        if (thread_comm_get_abort() != 0U)
        {
            main_task_force_outputs_safe_off();
        }
        /* Respond with actual state even when the driver rejects opening.
         * Otherwise GroundStation waits for the five-second periodic report. */
        (void)publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
        break;

    case CMD_NITROGEN_CLOSE:
        solenoidOff(&n2_solenoid);
        g_nitrogen_open = 0U;
        (void)publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
        publish_expected_outputs();
        break;

    case CMD_NITROUS_OPEN:
        if (solenoidOn(&n20_solenoid) == 0)
        {
            g_nitrous_open = 1U;
            publish_expected_outputs();
        }
        if (thread_comm_get_abort() != 0U)
        {
            main_task_force_outputs_safe_off();
        }
        (void)publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
        break;

    case CMD_NITROUS_CLOSE:
        solenoidOff(&n20_solenoid);
        g_nitrous_open = 0U;
        (void)publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
        publish_expected_outputs();
        break;

    case CMD_RETRACT_PLUMBING:
        start_plumbing_retract();
        break;

    case CMD_IGNITER_SEQUENCE:
        start_launch_sequence(cmd.timestamp_ms);
        break;
    
    default:
        break;
    }
}

void main_task_entry(ULONG initial_input)
{
    (void)initial_input;
    solenoidInit(&n2_solenoid);
    solenoidInit(&n20_solenoid);
    igniterInit(&igniter);
    stepperInit(&stepper);

    thread_comm_msg_t msg;
    ULONG last_umbilical_status_ticks = tx_time_get();

    publish_all_umbilical_statuses();
    publish_expected_outputs();

    for (;;)
    {
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

            while (thread_comm_receive(&msg, TX_NO_WAIT) == TX_SUCCESS)
            {
                // Drop pending commands while abort is latched.
            }

            publish_umbilical_statuses_if_due(&last_umbilical_status_ticks);
            retry_pending_status_reports();
            tx_thread_sleep(10);
            continue;
        }

        while (thread_comm_receive(&msg, TX_NO_WAIT) == TX_SUCCESS)
        {
            handle_command(msg);
        }

        service_launch_sequence();
        service_plumbing_retract();
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
