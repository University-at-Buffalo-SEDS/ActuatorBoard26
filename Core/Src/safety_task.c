#include "AB-Threads.h"
#include "tx_api.h"
#include "main.h"

#include <stdint.h>

TX_THREAD saftey_task_thread;
#define SAFETY_TASK_STACK_SIZE (4U * 1024U)


void saftey_task_entry(ULONG initial_input)
{
    while (1)
    {
        // Implement safety checks here
        // For example, monitor sensor data, check for faults, etc.

        // Sleep for a defined period before the next check
        tx_thread_sleep(100); // Sleep for 100 ticks (adjust as needed)
    }
}

UINT create_safety_task(TX_BYTE_POOL *byte_pool)
{
    CHAR *pointer;

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer, SAFETY_TASK_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
    {
        return TX_POOL_ERROR;
    }

    return tx_thread_create(&saftey_task_thread, "Safety Task", saftey_task_entry, 1, pointer, SAFETY_TASK_STACK_SIZE, 6, 6, TX_NO_TIME_SLICE, TX_AUTO_START);
}