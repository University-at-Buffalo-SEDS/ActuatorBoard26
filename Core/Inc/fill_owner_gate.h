#ifndef FILL_OWNER_GATE_H
#define FILL_OWNER_GATE_H
#include <stdint.h>
#include <stdbool.h>
/* Bounded callbacks only. No storage, SDK, sleeps or nested owner calls. */
bool thread_comm_fill_visit(void(*visit)(bool blocked,void*),void *context);
/* Source0 is an explicit local/read-only visit; network sources retain cutoff. */
bool thread_comm_fill_visit_epoch(uint64_t source,void(*visit)(bool blocked,uint32_t epoch,void*),void *context);
bool thread_comm_fill_config_begin(uint32_t epoch,uint8_t(*prepare)(void*),void *context);
bool thread_comm_fill_config_end(uint32_t epoch,void(*publish)(void*),void *context);
/* Why ordinary admission refuses now (board_refusal.h); takes the owner mutex,
 * so never call it from inside a visit. Also declared in thread_comm.h. */
uint8_t thread_comm_refusal_reason(uint64_t timestamp_ms,uint32_t epoch);
#endif
