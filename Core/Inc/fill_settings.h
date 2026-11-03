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
uint64_t fill_settings_cutoff(void);
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
