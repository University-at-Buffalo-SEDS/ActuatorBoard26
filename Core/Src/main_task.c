#include "AB-Threads.h"
#include "tx_api.h"
#include "thread_comm.h"
#include "main.h"
#include "telemetry.h"

#include <stdint.h>

TX_THREAD main_task_thread;
#define MAIN_TASK_STACK_SIZE (4U * 1024U)

static uint8_t g_aborted = 0U;

static void handle_command(thread_comm_msg_t cmd)
{
    switch (cmd)
    {
    case CMD_IGNITER_ON:
        HAL_GPIO_WritePin(IGNITER_PIN_GPIO_Port, IGNITER_PIN_Pin, GPIO_PIN_SET);
        break;

    case CMD_IGNITER_OFF:
        HAL_GPIO_WritePin(IGNITER_PIN_GPIO_Port, IGNITER_PIN_Pin, GPIO_PIN_RESET);
        break;

    default:
        break;
    }
}

void main_task_entry(ULONG initial_input)
{
    (void)initial_input;

    thread_comm_msg_t msg;

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

            tx_thread_sleep(10);
            continue;
        }

        while (thread_comm_receive(&msg, TX_NO_WAIT) == TX_SUCCESS)
        {
            handle_command(msg);
        }

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
                            0,
                            pointer,
                            MAIN_TASK_STACK_SIZE,
                            4,
                            4,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START);
}
