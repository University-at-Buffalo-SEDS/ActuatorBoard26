// telemetry_thread.c
#include "AB-Threads.h"
#include "tx_api.h"
#include "telemetry.h"
#include "can_bus.h"
#include "thread_comm.h"
#include "main.h"
#include "ota_stream.h"
TX_THREAD telemetry_thread;
#define TELEMETRY_THREAD_STACK_SIZE (16U * 1024U)

volatile uint32_t g_telemetry_stack_remaining = TELEMETRY_THREAD_STACK_SIZE;

static void sample_telemetry_stack(void)
{
    const volatile uint32_t *const start =
        (const volatile uint32_t *)telemetry_thread.tx_thread_stack_start;
    const volatile uint32_t *const end =
        (const volatile uint32_t *)telemetry_thread.tx_thread_stack_end;
    static const volatile uint32_t *high_water;
    if (start == NULL || end == NULL || start >= end) return;
    if (high_water == NULL || high_water < start || high_water > end) {
        high_water = start;
        while (high_water < end && *high_water == 0xEFEFEFEFUL) ++high_water;
    } else {
        while (high_water > start && high_water[-1] != 0xEFEFEFEFUL) --high_water;
    }
    const uint32_t remaining = (uint32_t)((uintptr_t)high_water -
        (uintptr_t)telemetry_thread.tx_thread_stack_start);
    if (remaining < g_telemetry_stack_remaining)
        g_telemetry_stack_remaining = remaining;
}

extern FDCAN_HandleTypeDef hfdcan2;

#ifdef TELEMETRY_TESTING
static void telemetry_disabled_command_cycle(void)
{
    static const thread_comm_msg_t on_commands[] = {
        // CMD_NITROGEN_OPEN,
        {.cmd = CMD_NITROGEN_OPEN, .timestamp_ms = 0ULL},
        {.cmd = CMD_NITROUS_OPEN, .timestamp_ms = 0ULL},
        
    };
    static const thread_comm_msg_t off_commands[] = {
        {.cmd = CMD_NITROGEN_CLOSE, .timestamp_ms = 0ULL},
        {.cmd = CMD_NITROUS_CLOSE, .timestamp_ms = 0ULL},
    };

    for (UINT i = 0; i < (UINT)(sizeof(on_commands) / sizeof(on_commands[0])); ++i)
    {
        (void)thread_comm_send(on_commands[i], TX_WAIT_FOREVER);
        tx_thread_sleep(1000);
    }
        tx_thread_sleep(1000);

    for (UINT i = 0; i < (UINT)(sizeof(off_commands) / sizeof(off_commands[0])); ++i)
    {
        (void)thread_comm_send(off_commands[i], TX_WAIT_FOREVER);
        tx_thread_sleep(1000);
    }
}
#endif

void telemetry_thread_entry(ULONG initial_input)
{
    (void)initial_input;
    can_bus_init(&hfdcan2);

#ifndef TELEMETRY_TESTING

    (void)init_telemetry_router();
#endif
#ifndef TELEMETRY_TESTING

    for (;;)
    {

        can_bus_process_rx();
        (void)process_rx_queue_timeout(0);
        (void)telemetry_poll_discovery();
        (void)telemetry_poll_timesync();
        ota_stream_poll();
        (void)dispatch_tx_queue_timeout(50);
        sample_telemetry_stack();
        tx_thread_sleep(1);
    }
#else
    for (;;)
    {
        can_bus_process_rx();

        telemetry_disabled_command_cycle();

        tx_thread_sleep(000);
    }
#endif
}

UINT create_telemetry_thread(TX_BYTE_POOL *byte_pool)
{

    CHAR *pointer;

    /* Allocate the stack for test  */
    if (tx_byte_allocate(byte_pool, (VOID **)&pointer,
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
