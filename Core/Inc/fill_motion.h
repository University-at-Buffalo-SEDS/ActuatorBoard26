#ifndef FILL_MOTION_H
#define FILL_MOTION_H
#include <stdint.h>
#include <stdbool.h>
/* Caller serializes all access through command owner. No SDK/storage/waits. */
enum { FILL_MODE_STEPPER=0,FILL_MODE_BRIDGE=1,FILL_MODE_AUX=2,FILL_MODE_UNKNOWN=255 };
enum { FILL_ACTION_NONE=0,FILL_ACTION_RETRACT=1,FILL_ACTION_EXTEND=2 };
enum { FILL_MOTION_STARTED=0,FILL_MOTION_DUPLICATE=1,FILL_MOTION_BUSY=2,FILL_MOTION_STALE=3,FILL_MOTION_UNSUPPORTED=4,FILL_MOTION_FAULT=5 };
/* How long the auxiliary drive holds BACKUP_VALVE_EN (PD2) for a QD retract.
 * The QD actuator's own limit switch ends its travel just before this (about
 * 20-25 s); the AB cannot sense that switch, so it stops on this timer and the
 * operator checks the QD by eye. If the actuator ever needs longer than 25 s
 * it is cut off short of its switch. The same window is the minimum-delay
 * provenance for either drive (fill_motion_status.c). */
#define FILL_DURATION_MS UINT32_C(25000)
/* last_source and source are packed (epoch, ms) stamps (board_stamp.h): a QD
 * stamped after a network time step in either direction orders after every
 * earlier one. fresh: the caller's SEDSnet window check of source (5 s old,
 * 1 s ahead, board_recovery_stamp_fresh), never a local clock comparison. */
typedef struct {uint64_t last_source;uint32_t started_ms;uint8_t last_action,active_action;bool active,completed;} FillMotion;
#define FILL_MOTION_FRESH_AGE_MS UINT64_C(5000)
#define FILL_MOTION_FRESH_AHEAD_MS UINT64_C(1000)
uint8_t fill_motion_start(FillMotion *m,uint8_t mode,uint8_t action,uint64_t source,bool fresh,uint32_t monotonic_ms);
/* True only once on a successful FILL_DURATION_MS completion; never on cancellation. */
bool fill_motion_service(FillMotion *m,uint32_t monotonic_ms);
void fill_motion_stop(FillMotion *m);
uint32_t fill_motion_remaining(const FillMotion *m,uint32_t monotonic_ms);
#endif
