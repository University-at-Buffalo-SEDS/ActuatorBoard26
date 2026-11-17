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
/* Caller holds the router: one nonce-0 Abort-latched BOARD_STATE_STATUS. */
SedsResult board_recovery_push_abort_status(SedsRouter *router, uint32_t uptime_ms);
uint8_t board_recovery_manual_stop_source_allowed(SedsRouter*router,const SedsPacketView*packet);
SedsResult board_recovery_handle(SedsRouter *router,const SedsPacketView *packet,
                                uint32_t local_endpoint,uint32_t uptime_ms);
/* Application replay floor; SDK stays generic. No current-time/RNG claim. */
uint8_t board_recovery_command_is_stale(const SedsPacketView *packet);
/* The board_refusal.h reason board_recovery_command_is_stale refuses for:
 * BELOW_FLOOR or STALE; NONE when the stamp passes. */
uint8_t board_recovery_command_stale_reason(const SedsPacketView *packet);
uint8_t board_recovery_managed_update_allowed(const SedsPacketView *packet);
/* Nonzero for a well-formed FLIGHT_STATE whose sender is not the station: a
 * state report, never a desired-state write. */
uint8_t board_recovery_managed_report_ignorable(const SedsPacketView *packet);
uint8_t board_recovery_automatic_is_stale(const SedsPacketView *packet);
/* Command-validity window of a packed (epoch, ms) stamp (board_stamp.h),
 * judged by SEDSnet against this node's network time across the steps it
 * remembers. A newer-epoch stamp passes while this node re-anchors to it; an
 * untranslatable older epoch or no network time never passes. NULL router:
 * the recovery router. */
uint8_t board_recovery_stamp_fresh(SedsRouter*router,uint64_t stamp,uint64_t max_age_ms,uint64_t max_ahead_ms);
/* Station request window (recovery, floor-active commands). */
#define BOARD_RECOVERY_FRESH_AGE_MS UINT64_C(15000)
#define BOARD_RECOVERY_FRESH_AHEAD_MS UINT64_C(1000)
uint8_t board_recovery_request_fresh(SedsRouter*router,uint64_t stamp);
/* Age of a packed stamp in this node's time base (negative: ahead). 0 when it
 * has no translatable age (newer epoch not yet adopted, no network time). */
uint8_t board_recovery_stamp_age(SedsRouter*router,uint64_t stamp,int64_t*age_ms);
#endif

/* Validated floor publication OUTSIDE owner locks. IO/mutex failure is not stale ACK. */
SedsResult board_recovery_sync_command_cutoff(void);
