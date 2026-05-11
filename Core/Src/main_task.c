#include "AB-Threads.h"
#include "tx_api.h"
#include "thread_comm.h"
#include "main.h"
#include "telemetry.h"
#include "igniter_driver.h"
#include "solenoid_driver.h"
#include "stepper_driver.h"

#include <stdint.h>

TX_THREAD main_task_thread;
#define MAIN_TASK_STACK_SIZE (4U * 1024U)
#define UMBILICAL_STATUS_PERIOD_TICKS TX_TIMER_TICKS_PER_SECOND
#define LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS 5000U
#define LAUNCH_SEQUENCE_IGNITER_ON_DURATION_MS 10000U
#define LAUNCH_SEQUENCE_RESTART_HOLDOFF_MS 1500U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_AGE_MS 5000U
#define LAUNCH_SEQUENCE_TIMESTAMP_MAX_FUTURE_MS 1000U

static uint8_t g_aborted = 0U;
static uint8_t g_igniter_on = 0U;
static uint8_t g_nitrogen_open = 0U;
static uint8_t g_nitrous_open = 0U;
static uint8_t g_plumbing_retracted = 0U;
static uint8_t g_launch_sequence_active = 0U;
static uint8_t g_launch_sequence_igniter_started = 0U;
static uint8_t g_launch_sequence_holdoff_active = 0U;
static ULONG g_launch_sequence_igniter_start_ticks = 0U;
static ULONG g_launch_sequence_igniter_stop_ticks = 0U;
static ULONG g_launch_sequence_holdoff_start_ticks = 0U;
igniter_t igniter = {IGNITER_PIN_GPIO_Port, IGNITER_PIN_Pin, IGNITER_FAULT_GPIO_Port, IGNITER_FAULT_Pin, 3};
solenoid_t n2_solenoid = {NITROGEN_PIN_GPIO_Port, NITROGEN_PIN_Pin, N2_SIG_GPIO_Port, N2_SIG_Pin, N2_FAULT_GPIO_Port, N2_FAULT_Pin, 2};
solenoid_t n20_solenoid = {NITROUS_PIN_GPIO_Port, NITROUS_PIN_Pin, N20_SIG_GPIO_Port, N20_SIG_Pin, N20_FAULT_GPIO_Port, N20_FAULT_Pin, 1};
stepper_t stepper = {STEPPER_CTRL_GPIO_Port, STEPPER_CTRL_Pin, STEPPER_DIR_GPIO_Port, STEPPER_DIR_Pin, STEPPER_RESET_GPIO_Port, STEPPER_RESET_Pin, STEPPER_SLEEP_GPIO_Port, STEPPER_SLEEP_Pin, false, STEP_CW};

static void publish_all_umbilical_statuses(void)
{
    (void)telemetry_publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
    (void)telemetry_publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
    (void)telemetry_publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
    (void)telemetry_publish_umbilical_status(CMD_RETRACT_PLUMBING, g_plumbing_retracted);
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
            (void)telemetry_publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
        }
        return 1U;
    }

    return 0U;
}

static void igniter_off(void)
{
    igniterOff(&igniter);
    g_igniter_on = 0U;
    (void)telemetry_publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
}

static void force_abort_outputs_off(void)
{
    g_launch_sequence_active = 0U;
    g_launch_sequence_igniter_started = 0U;
    g_launch_sequence_holdoff_active = 0U;

    igniter_off();
    solenoidOff(&n2_solenoid);
    solenoidOff(&n20_solenoid);
    stepperSleep(&stepper);

    HAL_GPIO_WritePin(BACKUP_VALVE_EN_GPIO_Port, BACKUP_VALVE_EN_Pin, GPIO_PIN_RESET);

    g_nitrogen_open = 0U;
    g_nitrous_open = 0U;
    (void)telemetry_publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
    (void)telemetry_publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
}

static ULONG launch_sequence_igniter_start_ticks(uint64_t packet_timestamp_ms)
{
    const ULONG now_ticks = tx_time_get();
    const ULONG start_delay_ticks = ms_to_ticks(LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS);

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

        if (packet_age_ms >= LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS)
        {
            return now_ticks;
        }

        return now_ticks + ms_to_ticks((uint32_t)(LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS - packet_age_ms));
    }

    const uint64_t packet_future_ms = packet_timestamp_ms - now_ms;
    if (packet_future_ms > LAUNCH_SEQUENCE_TIMESTAMP_MAX_FUTURE_MS)
    {
        return now_ticks + start_delay_ticks;
    }

    return now_ticks + ms_to_ticks((uint32_t)(packet_future_ms + LAUNCH_SEQUENCE_IGNITER_START_DELAY_MS));
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
    switch (cmd.cmd){
    case CMD_IGNITER_ON:
        g_launch_sequence_active = 0U;
        g_launch_sequence_igniter_started = 0U;
        g_launch_sequence_holdoff_active = 0U;
        igniter_on();
        break;

    case CMD_IGNITER_OFF:
        g_launch_sequence_active = 0U;
        g_launch_sequence_igniter_started = 0U;
        g_launch_sequence_holdoff_active = 0U;
        igniter_off();
        break;

    case CMD_NITROGEN_OPEN:
        solenoidOn(&n2_solenoid);
        g_nitrogen_open = 1U;
        (void)telemetry_publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
        break;

    case CMD_NITROGEN_CLOSE:
        solenoidOff(&n2_solenoid);
        g_nitrogen_open = 0U;
        (void)telemetry_publish_umbilical_status(CMD_NITROGEN_OPEN, g_nitrogen_open);
        break;

    case CMD_NITROUS_OPEN:
        solenoidOn(&n20_solenoid);
        g_nitrous_open = 1U;
        (void)telemetry_publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
        break;

    case CMD_NITROUS_CLOSE:
        solenoidOff(&n20_solenoid);
        g_nitrous_open = 0U;
        (void)telemetry_publish_umbilical_status(CMD_NITROUS_OPEN, g_nitrous_open);
        break;

    case CMD_RETRACT_PLUMBING:
        stepperSetDir(&stepper, STEP_CW);
        stepperWake(&stepper);
        stepperMoveSteps(&stepper, 20000, 80);
        stepperSleep(&stepper);
        g_plumbing_retracted = 1U;
        (void)telemetry_publish_umbilical_status(CMD_RETRACT_PLUMBING, g_plumbing_retracted);
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

    for (;;)
    {
        if (thread_comm_get_abort() != 0U)
        {
            if (g_aborted == 0U)
            {
                force_abort_outputs_off();
                g_aborted = 1U;
            }

            while (thread_comm_receive(&msg, TX_NO_WAIT) == TX_SUCCESS)
            {
                // Dump any pending messages bc we ABORTING!!!!!!!
            }

            publish_umbilical_statuses_if_due(&last_umbilical_status_ticks);
            tx_thread_sleep(10);
            continue;
        }

        while (thread_comm_receive(&msg, TX_NO_WAIT) == TX_SUCCESS)
        {
            handle_command(msg);
        }

        service_launch_sequence();
        publish_umbilical_statuses_if_due(&last_umbilical_status_ticks);

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
