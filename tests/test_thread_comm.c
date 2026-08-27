#include "thread_comm.h"
#include <assert.h>

UINT tx_mutex_create(TX_MUTEX *mutex, const char *name, UINT inherit)
{ (void)name; (void)inherit; mutex->locked = 0U; return TX_SUCCESS; }
UINT tx_mutex_get(TX_MUTEX *mutex, ULONG wait_option)
{ (void)wait_option; if (mutex->locked) return TX_MUTEX_ERROR; mutex->locked = 1U; return TX_SUCCESS; }
UINT tx_mutex_put(TX_MUTEX *mutex)
{ if (!mutex->locked) return TX_MUTEX_ERROR; mutex->locked = 0U; return TX_SUCCESS; }
UINT tx_semaphore_create(TX_SEMAPHORE *semaphore, const char *name, ULONG count)
{ (void)name; semaphore->count = count; return TX_SUCCESS; }
UINT tx_semaphore_get(TX_SEMAPHORE *semaphore, ULONG wait_option)
{ (void)wait_option; if (semaphore->count == 0U) return TX_NO_INSTANCE; semaphore->count--; return TX_SUCCESS; }
UINT tx_semaphore_put(TX_SEMAPHORE *semaphore)
{ semaphore->count++; return TX_SUCCESS; }

int main(void)
{
    TX_BYTE_POOL pool = {0};
    assert(thread_comm_init(&pool) == TX_SUCCESS);
    assert(thread_comm_init(&pool) == TX_SUCCESS);

    for (uint8_t i = 0U; i < THREAD_COMM_QUEUE_DEPTH; ++i)
    {
        thread_comm_msg_t msg = {.cmd = i, .timestamp_ms = 1000ULL + i};
        assert(thread_comm_send(msg, TX_NO_WAIT) == TX_SUCCESS);
    }
    const thread_comm_msg_t overflow = {.cmd = 99U, .timestamp_ms = 0ULL};
    assert(thread_comm_send(overflow, TX_NO_WAIT) == TX_NO_INSTANCE);

    for (uint8_t i = 0U; i < THREAD_COMM_QUEUE_DEPTH; ++i)
    {
        thread_comm_msg_t msg;
        assert(thread_comm_receive(&msg, TX_NO_WAIT) == TX_SUCCESS);
        assert(msg.cmd == i);
        assert(msg.timestamp_ms == 1000ULL + i);
    }
    thread_comm_msg_t empty;
    assert(thread_comm_receive(&empty, TX_NO_WAIT) == TX_NO_INSTANCE);

    assert(thread_comm_set_shared_value(-42) == TX_SUCCESS);
    assert(thread_comm_get_shared_value() == -42);
    assert(thread_comm_set_flight_state(ACTUATOR_FLIGHT_STATE_ARMED) == TX_SUCCESS);
    assert(thread_comm_get_flight_state() == ACTUATOR_FLIGHT_STATE_ARMED);
    assert(thread_comm_abort_allowed() == 1U);
    assert(thread_comm_set_expected_outputs(2U, 0U, 7U) == TX_SUCCESS);
    const thread_comm_expected_outputs_t outputs = thread_comm_get_expected_outputs();
    assert(outputs.n20_on == 1U && outputs.n2_on == 0U && outputs.igniter_on == 1U);
    assert(thread_comm_note_groundstation_heartbeat(123456ULL) == TX_SUCCESS);
    assert(thread_comm_get_groundstation_heartbeat_ms() == 123456ULL);
    assert(thread_comm_set_abort(1U) == TX_SUCCESS);
    assert(thread_comm_set_abort(0U) == TX_SUCCESS);
    assert(thread_comm_get_abort() == 1U);
    return 0;
}
