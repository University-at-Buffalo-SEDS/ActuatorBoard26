#pragma once

#include "tx_api.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of message slots allocated for the inter-thread queue.
 */
#define THREAD_COMM_QUEUE_DEPTH 16U

typedef struct {
    uint8_t cmd;
    uint64_t timestamp_ms;
} thread_comm_msg_t;

/* Application-local receipt, never a SEDSnet wire packet ID. */
#define THREAD_COMM_LAUNCH_SENDER_MAX 32U
typedef struct { uint64_t source_utc; uint32_t epoch, serial; } thread_comm_launch_owner_t;
uint8_t thread_comm_launch_continuity(void);
uint8_t thread_comm_launch_original_accepted(uint64_t source,const char*sender,uint32_t sender_len);
uint8_t thread_comm_launch_claim(uint64_t source,uint32_t epoch,thread_comm_launch_owner_t*out);
uint8_t thread_comm_launch_begin_owned(const thread_comm_launch_owner_t*owner,void(*apply)(void*),void*context);
/* Original network UTC is mapped once, before acceptance, to a bounded
 * monotonic effect deadline. No UTC lookup or retiming after acceptance. */
#define THREAD_COMM_LAUNCH_EFFECT_DELAY_MS 4250U
uint8_t thread_comm_launch_begin_owned_deadline(const thread_comm_launch_owner_t*owner,void(*apply)(void*),void*context,uint32_t*deadline);

uint8_t thread_comm_launch_effect_owned(const thread_comm_launch_owner_t*owner,void(*apply)(void*),void*context);
void thread_comm_launch_complete_owned(const thread_comm_launch_owner_t*owner);
UINT thread_comm_admit_network_launch_original(thread_comm_msg_t msg,const char*sender,uint32_t sender_len);
UINT thread_comm_admit_network_launch_original_at(thread_comm_msg_t msg,const char*sender,uint32_t sender_len,uint64_t observed_utc,uint32_t observed_tick,uint32_t(*monotonic_now)(void));
/* Network FLIGHT_STATE (contract set (a)): Startup, Idle, Fill, Armed, Launch,
 * Landed, Aborted and InFlight. RETIRED members are never on the wire: a
 * receiver maps them on receipt (flight_state_cache.h). The names stay
 * only so older firmware on a mixed bench reads clearly. */
typedef enum {
    ACTUATOR_FLIGHT_STATE_STARTUP = 0,
    ACTUATOR_FLIGHT_STATE_IDLE = 1,
    ACTUATOR_FLIGHT_STATE_FILL = 2,
    ACTUATOR_FLIGHT_STATE_RETIRED_FILL_TEST = 3,
    ACTUATOR_FLIGHT_STATE_RETIRED_NITROGEN_FILL = 4,
    ACTUATOR_FLIGHT_STATE_RETIRED_NITROUS_FILL = 5,
    ACTUATOR_FLIGHT_STATE_ARMED = 6,
    ACTUATOR_FLIGHT_STATE_LAUNCH = 7,
    ACTUATOR_FLIGHT_STATE_RETIRED_ASCENT = 8,
    ACTUATOR_FLIGHT_STATE_RETIRED_COAST = 9,
    ACTUATOR_FLIGHT_STATE_RETIRED_APOGEE = 10,
    ACTUATOR_FLIGHT_STATE_RETIRED_PARACHUTE_DEPLOY = 11,
    ACTUATOR_FLIGHT_STATE_RETIRED_DESCENT = 12,
    ACTUATOR_FLIGHT_STATE_LANDED = 13,
    ACTUATOR_FLIGHT_STATE_RETIRED_RECOVERY = 14,
    ACTUATOR_FLIGHT_STATE_ABORTED = 15,
    ACTUATOR_FLIGHT_STATE_IN_FLIGHT = 16,
} actuator_flight_state_t;

/* Launch, InFlight and Landed: the vehicle has launched (Aborted excluded).
 * The retired post-launch values 8..12 and 14 count too, so a state set
 * before mapping can never read as pre-launch. */
static inline uint8_t flight_state_launched(uint8_t state)
{
    return (uint8_t)((state >= ACTUATOR_FLIGHT_STATE_LAUNCH && state <= ACTUATOR_FLIGHT_STATE_RETIRED_RECOVERY) ||
                     state == ACTUATOR_FLIGHT_STATE_IN_FLIGHT);
}

/* Progress order: Launch (7) < InFlight (16) < Landed (13). InFlight ranks
 * with the retired in-flight values it replaces (8..12). */
static inline uint8_t flight_state_rank(uint8_t state)
{
    return state == ACTUATOR_FLIGHT_STATE_IN_FLIGHT ? ACTUATOR_FLIGHT_STATE_RETIRED_DESCENT : state;
}

typedef struct {
    uint8_t n20_on;
    uint8_t n2_on;
    uint8_t igniter_on;
} thread_comm_expected_outputs_t;

/**
 * @brief Create the shared queue and state protection mutex.
 *
 * This should be called once during system startup before any thread uses the
 * communication helpers.
 *
 * @param byte_pool ThreadX byte pool used to allocate queue storage.
 * @retval TX_SUCCESS Initialization completed successfully.
 * @retval ThreadX error code Queue or mutex creation/allocation failed.
 */
UINT thread_comm_init(TX_BYTE_POOL *byte_pool);

/**
 * @brief Push one message into the shared queue.
 *
 * Safe to call from a producer thread while another thread drains the queue.
 *
 * @param msg Message value to enqueue.
 * @param wait_option ThreadX wait option passed to tx_queue_send().
 * @retval TX_SUCCESS Message was queued.
 * @retval ThreadX error code Queue was not ready, full, or send failed.
 */
UINT thread_comm_send(thread_comm_msg_t msg, ULONG wait_option);
/* Application-internal outcomes, never SDK wire IDs or fake completion. */
#define THREAD_COMM_STALE_TIMESTAMP ((UINT)0xF001U)
#define THREAD_COMM_COMMIT_UNCERTAIN ((UINT)0xF002U)
UINT thread_comm_publish_command_cutoff(uint64_t cutoff_ms);
UINT thread_comm_admit_network(thread_comm_msg_t msg, uint64_t timestamp_ms);
UINT thread_comm_apply_network_flight_state(uint8_t state,uint64_t timestamp_ms);
UINT thread_comm_admit_network_automatic(thread_comm_msg_t msg, uint64_t timestamp_ms);
UINT thread_comm_admit_network_launch(thread_comm_msg_t msg);
uint8_t thread_comm_command_timestamp_current(uint64_t timestamp_ms);

/**
 * @brief Pop one message from the shared queue.
 *
 * Intended for the consumer thread that drains work produced by another
 * thread.
 *
 * @param msg Output pointer that receives the dequeued message.
 * @param wait_option ThreadX wait option passed to tx_queue_receive().
 * @retval TX_SUCCESS Message was received.
 * @retval ThreadX error code Queue was not ready, empty, or receive failed.
 */
UINT thread_comm_receive(thread_comm_msg_t *msg, ULONG wait_option);

/**
 * @brief Latch the shared abort flag.
 *
 * The flag is protected by a mutex so one thread can request an abort while
 * other threads read a consistent value. Once set, the flag remains latched
 * until the board is power cycled.
 *
 * @param abort_requested Non-zero sets the flag, zero is ignored.
 * @retval TX_SUCCESS Flag updated successfully.
 * @retval ThreadX error code Mutex access failed.
 */
UINT thread_comm_set_abort(uint8_t abort_requested);
/* One newly delivered operator Stop; advances recovery epoch even if latched. */
UINT thread_comm_request_manual_abort(void);
UINT thread_comm_request_manual_abort_original_utc(uint64_t source);
/* Task-context bounded GPIO/RAM only callbacks. No SDK/storage/delay/reentry. */
UINT thread_comm_bind_manual_stop_apply(void (*apply)(void));
uint8_t thread_comm_manual_effect(uint64_t original_source,void (*apply)(void*),void*context);


/**
 * @brief Read the shared abort flag.
 *
 * @return `1` if abort has been requested, otherwise `0`.
 */
uint8_t thread_comm_get_abort(void);

/**
 * @brief Store the shared integer value.
 *
 * @param value New value to publish for other threads.
 * @retval TX_SUCCESS Value updated successfully.
 * @retval ThreadX error code Mutex access failed.
 */
UINT thread_comm_set_shared_value(int32_t value);

/**
 * @brief Read the latest shared integer value.
 *
 * @return Most recently stored shared value, or `0` if the module is not yet
 * initialized.
 */
int32_t thread_comm_get_shared_value(void);

UINT thread_comm_set_flight_state(uint8_t flight_state);
/* Managed state is not manual Abort provenance; admission is atomic with launch. */
UINT thread_comm_apply_managed_flight_state(uint8_t flight_state);
uint8_t thread_comm_get_flight_state(void);
uint8_t thread_comm_abort_allowed(void);
/* Overpressure is also admitted in Startup; never after Launch. */
uint8_t thread_comm_request_automatic_abort(uint8_t overpressure);
/* Owner-tagged finite local report. A lease is immutable through SDK return.
 * No state mutex or IRQ mask is held by the caller during SDK enqueue. */
typedef struct {
    uint64_t original_utc;
    uint32_t reason;
    uint32_t epoch;
    uint32_t lease;
} thread_comm_safety_report_t;
uint8_t thread_comm_request_local_abort(uint32_t reason, uint64_t at_ms,
    uint64_t heartbeat_ms, uint32_t observed_state, uint32_t detail,
    uint64_t original_utc, uint8_t heartbeat_only);
uint8_t thread_comm_safety_report_claim(thread_comm_safety_report_t *out);
void thread_comm_safety_report_finish(uint32_t lease, uint8_t retained);
void thread_comm_service_safety_publication(void);

/* Link loss excludes Idle; other automatic safety rules remain unchanged. */
uint8_t thread_comm_request_heartbeat_abort(void);
uint8_t thread_comm_heartbeat_abort_allowed(void);
/* Exact typed automatic ordinary-valve commands only; no launch/abort/pyro. */
#define THREAD_COMM_AUTOMATIC_TAG 0x80U
UINT thread_comm_admit_automatic(thread_comm_msg_t msg);
/* Callback must contain only bounded GPIO/timer/RAM operations, no SDK/IO wait.
 * Launch and auto-application serialize through this same state mutex. */
uint8_t thread_comm_apply_automatic(thread_comm_msg_t msg, void (*apply)(uint8_t));
UINT thread_comm_admit_launch(thread_comm_msg_t msg);

UINT thread_comm_set_expected_outputs(uint8_t n20_on, uint8_t n2_on, uint8_t igniter_on);
thread_comm_expected_outputs_t thread_comm_get_expected_outputs(void);

UINT thread_comm_note_groundstation_heartbeat(uint64_t timestamp_ms);
uint64_t thread_comm_get_groundstation_heartbeat_ms(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* Captures cancellation epoch with actual nonwaiting dequeue, no wire change. */
UINT thread_comm_receive_owned(thread_comm_msg_t*message,uint32_t*epoch);
uint8_t thread_comm_manual_effect_owned(uint64_t original_source,uint32_t epoch,void(*apply)(void*),void*context);
uint32_t thread_comm_manual_epoch(void);

uint8_t thread_comm_apply_automatic_owned(thread_comm_msg_t msg,uint32_t epoch,void(*apply)(uint8_t));
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* OTA reset effect only: -1 busy,0 retired/unsafe,1 current (or executed).
 * No storage/SDK/work is allowed in apply. NULL performs same owned check. */
int thread_comm_ota_reset_original(uint32_t epoch,void(*apply)(void*),void*context);
#ifdef __cplusplus
}
#endif
