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

typedef enum {
    ACTUATOR_FLIGHT_STATE_STARTUP = 0,
    ACTUATOR_FLIGHT_STATE_IDLE = 1,
    ACTUATOR_FLIGHT_STATE_PREFILL = 2,
    ACTUATOR_FLIGHT_STATE_FILL_TEST = 3,
    ACTUATOR_FLIGHT_STATE_NITROGEN_FILL = 4,
    ACTUATOR_FLIGHT_STATE_NITROUS_FILL = 5,
    ACTUATOR_FLIGHT_STATE_ARMED = 6,
    ACTUATOR_FLIGHT_STATE_LAUNCH = 7,
    ACTUATOR_FLIGHT_STATE_ASCENT = 8,
    ACTUATOR_FLIGHT_STATE_COAST = 9,
    ACTUATOR_FLIGHT_STATE_APOGEE = 10,
    ACTUATOR_FLIGHT_STATE_PARACHUTE_DEPLOY = 11,
    ACTUATOR_FLIGHT_STATE_DESCENT = 12,
    ACTUATOR_FLIGHT_STATE_LANDED = 13,
    ACTUATOR_FLIGHT_STATE_RECOVERY = 14,
    ACTUATOR_FLIGHT_STATE_ABORTED = 15,
} actuator_flight_state_t;

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
uint8_t thread_comm_get_flight_state(void);
uint8_t thread_comm_abort_allowed(void);

UINT thread_comm_set_expected_outputs(uint8_t n20_on, uint8_t n2_on, uint8_t igniter_on);
thread_comm_expected_outputs_t thread_comm_get_expected_outputs(void);

UINT thread_comm_note_groundstation_heartbeat(uint64_t timestamp_ms);
uint64_t thread_comm_get_groundstation_heartbeat_ms(void);

#ifdef __cplusplus
}
#endif
