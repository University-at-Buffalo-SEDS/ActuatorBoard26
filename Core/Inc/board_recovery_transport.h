#ifndef BOARD_RECOVERY_TRANSPORT_H
#define BOARD_RECOVERY_TRANSPORT_H
#include "sedsnet.h"
#include "sedsnet_config.h"
/* Reliable packets delivered but deliberately not applied: each is counted
 * and acknowledged, so the station stops retransmitting it. */
extern volatile uint32_t g_reliable_refusal_count;
/* SEDS_BAD_ARG/SEDS_PERMISSION_DENIED (a deterministic refusal) is counted
 * and becomes SEDS_OK; transient failures pass through and stay retried. */
SedsResult board_recovery_acked_refusal(SedsResult result);
void board_recovery_transport_set_router(SedsRouter *router);
uint8_t board_recovery_manual_stop_source_allowed(SedsRouter*router,const SedsPacketView*packet);
SedsResult board_recovery_handle(SedsRouter *router,const SedsPacketView *packet,
                                uint32_t local_endpoint,uint32_t uptime_ms);
/* Application replay floor; SDK stays generic. No current-time/RNG claim. */
uint8_t board_recovery_command_is_stale(const SedsPacketView *packet);
uint8_t board_recovery_managed_update_allowed(const SedsPacketView *packet);
uint8_t board_recovery_automatic_is_stale(const SedsPacketView *packet);
/* Advance the station clock from an authorized GS packet's timestamp. */
void board_recovery_observe_station_time(SedsRouter*router,const SedsPacketView*packet);
/* Freshness clock: max(valid network time, station clock); 0 when neither. */
uint64_t board_recovery_station_now(SedsRouter*router);
/* Network thread only: a valid network clock more than 60 s from the station
 * clock (which then restarts from the next GS packet). */
bool board_recovery_network_clock_resync_needed(uint64_t network_now);
#endif

/* Validated floor publication OUTSIDE owner locks. IO/mutex failure is not stale ACK. */
SedsResult board_recovery_sync_command_cutoff(void);
