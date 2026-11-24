#include "thread_comm.h"
#include "board_refusal.h"
#include "board_stamp.h"
#include "board_commands.h"
#include <assert.h>
#include <stdbool.h>
/* Queue/state-only unit: storage is already healthy, no commissioning is in
 * flight, and legacy/manual Stop is never retired. Actual journal, pending,
 * failure and retirement behavior is covered by the strict recovery fixtures. */
bool persistent_store_controls_ready(void) { return true; }
bool persistent_store_recovery_ready(void) { return true; }
bool persistent_store_bootstrap_pending(void) { return false; }
void persistent_store_hold_bootstrap(void) { assert(!"Unexpected storage hold in healthy queue unit"); }
bool persistent_store_publish_bootstrap_complete(void) { assert(!"No bootstrap publish in queue unit"); return false; }
uint8_t board_recovery_manual_time_retired(uint64_t source) { (void)source; return 0U; }

static uint8_t retained_abort, retained_launch;
void board_launch_latch_set(void) { retained_launch=1U; }
uint8_t board_launch_latch_set_verified(void) { board_launch_latch_set(); return 1U; }
uint8_t board_launch_latch_is_set(void) { return retained_launch; }
uint8_t board_launch_latch_was_verified(void){ return retained_launch; }

void board_abort_latch_set(void) { retained_abort=1U; }
uint8_t board_abort_latch_prepare_recovery(void) { retained_abort=1U; return 1U; }
uint8_t board_abort_latch_clear_for_recovery(void) { retained_abort=retained_launch=0U; return 1U; }
uint8_t board_abort_latch_is_set(void) { return retained_abort; }

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

static uint32_t fixed_tick(void) { return 100U; }

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

    /* A refused operator command records why for the station (board_refusal.h). */
    assert(board_refusal_current() == 0U);
    assert(thread_comm_refusal_reason(4000ULL, 0U) == BOARD_REFUSAL_NONE);
    assert(thread_comm_publish_command_cutoff(5000ULL) == TX_SUCCESS);
    const thread_comm_msg_t late = {.cmd = 8U, .timestamp_ms = 4000ULL};
    assert(thread_comm_admit_network(late, late.timestamp_ms) == THREAD_COMM_STALE_TIMESTAMP);
    assert(board_refusal_current() == ((1U << 16) | (8U << 8) | BOARD_REFUSAL_BELOW_FLOOR));
    const thread_comm_msg_t automatic = {.cmd = 0x80U | 9U, .timestamp_ms = 4000ULL};
    assert(thread_comm_admit_network(automatic, automatic.timestamp_ms) == THREAD_COMM_STALE_TIMESTAMP);
    assert(board_refusal_current() >> 16 == 1U); /* GS sequence commands are not recorded */
    const uint32_t epoch = thread_comm_manual_epoch();
    assert(thread_comm_refusal_reason(6000ULL, epoch) == BOARD_REFUSAL_NONE);
    assert(thread_comm_refusal_reason(4000ULL, epoch) == BOARD_REFUSAL_BELOW_FLOOR);
    assert(thread_comm_refusal_reason(6000ULL, epoch + 1U) == BOARD_REFUSAL_RECOVERY_PENDING);
    const thread_comm_msg_t fresh = {.cmd = 9U, .timestamp_ms = 6000ULL};
    assert(thread_comm_admit_network(fresh, fresh.timestamp_ms) == TX_SUCCESS);
    assert(board_refusal_current() >> 16 == 1U); /* an accepted command records nothing */
    thread_comm_msg_t drained;
    assert(thread_comm_receive(&drained, TX_NO_WAIT) == TX_SUCCESS && drained.cmd == 9U);

    /* A Launch whose stamp is too old for its effect deadline (station stamp
     * 14 s before the board's network time) is refused with a reason, not only
     * by withholding its receipt. */
    {
        const uint32_t refusals = thread_comm_launch_refusal_count();
        const thread_comm_msg_t launch = {.cmd = CMD_IGNITER_SEQUENCE, .timestamp_ms = board_stamp_pack(0U, 6000ULL)};
        assert(thread_comm_admit_network_launch_original_at(launch, "GS", 2U, 20000ULL, 100U, fixed_tick) != TX_SUCCESS);
        assert(thread_comm_launch_refusal_count() == refusals + 1U);
        assert(thread_comm_launch_last_refusal() == BOARD_REFUSAL_STALE);
        /* a stamp ahead of the board by more than the allowed 1 s: refused too */
        const thread_comm_msg_t ahead = {.cmd = CMD_IGNITER_SEQUENCE, .timestamp_ms = board_stamp_pack(0U, 9000ULL)};
        assert(thread_comm_admit_network_launch_original_at(ahead, "GS", 2U, 7000ULL, 100U, fixed_tick) != TX_SUCCESS);
        assert(thread_comm_launch_refusal_count() == refusals + 2U);
        assert(thread_comm_launch_last_refusal() == BOARD_REFUSAL_STALE);
    }

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
    assert(retained_abort == 1U);
    assert(thread_comm_refusal_reason(6000ULL, 0U) == BOARD_REFUSAL_ABORT);
    assert(thread_comm_admit_network(fresh, 7000ULL) == THREAD_COMM_STALE_TIMESTAMP);
    assert(board_refusal_current() == ((2U << 16) | (9U << 8) | BOARD_REFUSAL_ABORT));
    return 0;
}
