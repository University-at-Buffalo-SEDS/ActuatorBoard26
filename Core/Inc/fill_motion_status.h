#ifndef FILL_MOTION_STATUS_H
#define FILL_MOTION_STATUS_H
#include <stdint.h>
#include <stdbool.h>
#define FILL_MOTION_STATUS_V2_SIZE 56U
#define FILL_MOTION_QUERY_VERSION 2U
enum {FILL_PROVENANCE_UNKNOWN=0,FILL_PROVENANCE_ACTIVE=1,FILL_PROVENANCE_COMPLETED=2,FILL_PROVENANCE_CANCELLED=3,FILL_PROVENANCE_FAULT=4};
typedef struct {
 uint64_t source;
 uint32_t started_ms,origin_epoch;
 uint8_t action,lifecycle;
 bool minimum_satisfied;
} FillMotionProvenance;
typedef struct {
 uint64_t accepted_source;
 uint32_t current_epoch,origin_epoch,remaining_ms,minimum_remaining_ms;
 uint8_t mode,active_action,busy,lifecycle,accepted_action;
 bool fault;
} FillMotionSnapshotV2;
/* All provenance operations require the existing serialized fill/state owner. */
void fill_motion_provenance_start(FillMotionProvenance *,uint64_t source,uint32_t epoch,uint8_t action,uint32_t actual_start_ms);
void fill_motion_provenance_complete(FillMotionProvenance *);
void fill_motion_provenance_cancel(FillMotionProvenance *,bool fault);
uint32_t fill_motion_provenance_remaining(FillMotionProvenance *,uint32_t monotonic_ms);
void fill_motion_status_v2_encode(uint8_t out[FILL_MOTION_STATUS_V2_SIZE],const uint8_t request[32],uint8_t result,uint64_t generation,const FillMotionSnapshotV2 *snapshot);
#endif
