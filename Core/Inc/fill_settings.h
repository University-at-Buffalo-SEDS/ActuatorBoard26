#ifndef FILL_SETTINGS_H
#define FILL_SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
#include "fill_motion_status.h"
/* Main owner boot restore (no writes) and periodic work. */
void fill_settings_restore(void);
void fill_settings_service(void);
uint8_t fill_settings_mode(void);
bool fill_settings_ready(void);
/* Packed (epoch, ms) stamp of the persisted configuration (board_stamp.h); a
 * record written before epochs reads as epoch 0. QD and legacy retracts order
 * after it. */
uint64_t fill_settings_cutoff(void);
/* Config identity for the station's resync, reported in AB_FILL_DRIVE_STATUS
 * bytes 34..35 (LE): a 16-bit fold of the saving station's request time (the
 * ms part of the record's stamp), ms ^ ms>>16 ^ ms>>32, never 0; 0 when no
 * station saved the selection (absent record: the factory stepper default) or
 * it is not loaded. The station keeps the request times it sent. */
uint16_t fill_settings_identity(void);
uint16_t fill_settings_identity_of(uint64_t source);
/* Bounded serialized main-owner callbacks, no SDK or storage. */
uint8_t main_task_fill_prepare_mode(uint8_t mode);
void main_task_fill_mode_changed(void);
bool main_task_fill_snapshot(uint8_t *mode,uint8_t *action,uint8_t *busy,uint32_t *remaining);
bool main_task_fill_snapshot_v2(FillMotionSnapshotV2 *snapshot);
#ifdef TELEMETRY_ENABLED
#include "sedsnet.h"
SedsResult fill_settings_handle(SedsRouter *router,const SedsPacketView *packet);
#endif
#endif
