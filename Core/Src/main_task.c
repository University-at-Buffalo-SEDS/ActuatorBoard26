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

static uint8_t g_aborted = 0U;
static uint8_t g_igniter_on = 0U;
static uint8_t g_nitrogen_open = 0U;
static uint8_t g_nitrous_open = 0U;
static uint8_t g_plumbing_retracted = 0U;
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

static void handle_command(thread_comm_msg_t cmd)
{
    switch (cmd){
    case CMD_IGNITER_ON:
        igniterOn(&igniter);
        g_igniter_on = 1U;
        (void)telemetry_publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
        break;

    case CMD_IGNITER_OFF:
        igniterOff(&igniter);
        g_igniter_on = 0U;
        (void)telemetry_publish_umbilical_status(CMD_IGNITER_ON, g_igniter_on);
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
                HAL_GPIO_WritePin(IGNITER_PIN_GPIO_Port, IGNITER_PIN_Pin, GPIO_PIN_RESET);
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
