// telemetry_thread.c
#include "AB-Threads.h"
#include "tx_api.h"
#include "telemetry.h"
#include "can_bus.h"
#include "thread_comm.h"
#include "main.h"

TX_THREAD telemetry_thread;
#define TELEMETRY_THREAD_STACK_SIZE (16U * 1024U)

extern FDCAN_HandleTypeDef hfdcan2;

#ifndef TELEMETRY_ENABLED
static void telemetry_disabled_command_cycle(void)
{
    static const thread_comm_msg_t on_commands[] = {
        CMD_IGNITER_ON,
        CMD_NITROGEN_OPEN,
        CMD_NITROUS_OPEN,
    };
    static const thread_comm_msg_t off_commands[] = {
        CMD_IGNITER_OFF,
        CMD_NITROGEN_CLOSE,
        CMD_NITROUS_CLOSE,
    };

    for (UINT i = 0; i < (UINT)(sizeof(on_commands) / sizeof(on_commands[0])); ++i)
    {
        (void)thread_comm_send(on_commands[i], TX_WAIT_FOREVER);
        tx_thread_relinquish();
    }

    for (UINT i = 0; i < (UINT)(sizeof(off_commands) / sizeof(off_commands[0])); ++i)
    {
        (void)thread_comm_send(off_commands[i], TX_WAIT_FOREVER);
        tx_thread_relinquish();
    }
}
#endif

void telemetry_thread_entry(ULONG initial_input)
{
    (void)initial_input;

    can_bus_init(&hfdcan2);
#ifdef TELEMETRY_ENABLED
    (void)init_telemetry_router();
#endif

    for (;;) {
        can_bus_process_rx();
#ifdef TELEMETRY_ENABLED
        (void)telemetry_poll_discovery();
        (void)process_all_queues_timeout(0);
        (void)telemetry_poll_timesync();
        tx_thread_relinquish();

#else
        tx_thread_relinquish();
        // telemetry_disabled_command_cycle();
#endif
    }
}

UINT create_telemetry_thread(TX_BYTE_POOL *byte_pool)
{

        CHAR *pointer;

  /* Allocate the stack for test  */
  if (tx_byte_allocate(byte_pool, (VOID**) &pointer,
                       TELEMETRY_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

    UINT status = tx_thread_create(&telemetry_thread,
                                   "Telemetry Thread",
                                   telemetry_thread_entry,
                                   0,
                                   pointer,
                                   TELEMETRY_THREAD_STACK_SIZE,
                                   5,
                                   5,
                                   TX_NO_TIME_SLICE,
                                   TX_AUTO_START);

    return status;
}
