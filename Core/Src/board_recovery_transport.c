#include "persistent_store_health.h"
#include "board_recovery_transport.h"
#include "board_recovery_wire.h"
#include "board_recovery_boot.h"
#include "board_recovery_floor.h"
#include "board_recovery.h"
#include "flight_state_cache.h"
#include "thread_comm.h"
#include "board_bootstrap_storage.h"
#include "bootstrap_owner.h"
#include "bootstrap_marker.h"
#include "board_stamp.h"
#include "board_refusal.h"
#include <stdatomic.h>
#include <string.h>
static SedsRouter *recovery_router;
/* Reliable packets delivered but deliberately not applied, then acknowledged. */
volatile uint32_t g_reliable_refusal_count __attribute__((used));
SedsResult board_recovery_acked_refusal(SedsResult result)
{
 if(result!=SEDS_BAD_ARG&&result!=SEDS_PERMISSION_DENIED)return result;
 ++g_reliable_refusal_count;return SEDS_OK;
}
void board_recovery_transport_set_router(SedsRouter *router) { recovery_router=router; }
static atomic_flag dispatching=ATOMIC_FLAG_INIT;
static uint8_t authorized_primary(SedsRouter *router,const SedsPacketView *packet)
{
 return packet!=NULL && seds_router_sender_matches_hostname(router,
     packet->sender,packet->sender_len,"GS",2U);
}
uint8_t board_recovery_manual_stop_source_allowed(SedsRouter*router,const SedsPacketView*packet){
 return router&&packet&&(authorized_primary(router,packet)||seds_router_sender_matches_hostname(router,packet->sender,packet->sender_len,"RRLS",4U));
}
SedsResult board_recovery_sync_command_cutoff(void)
{
 uint64_t cutoff=0U;
 if (!board_recovery_floor_read_cutoff(&cutoff)) return SEDS_IO;
 return cutoff==0U || thread_comm_publish_command_cutoff(cutoff)==TX_SUCCESS ? SEDS_OK : SEDS_IO;
}
/* SEDSnet judges a (epoch, ms) stamp across the network time steps this node
 * remembers; no remote stamp is ever compared with a local clock here. */
uint8_t board_recovery_stamp_fresh(SedsRouter*router,uint64_t stamp,uint64_t max_age_ms,uint64_t max_ahead_ms){
 bool fresh=false;if(!router)router=recovery_router;
 return router&&stamp&&stamp!=UINT64_MAX&&
  seds_router_network_stamp_fresh(router,board_stamp_epoch(stamp),board_stamp_ms(stamp),max_age_ms,max_ahead_ms,&fresh)==SEDS_OK&&fresh;
}
uint8_t board_recovery_request_fresh(SedsRouter*router,uint64_t stamp){
 return board_recovery_stamp_fresh(router,stamp,BOARD_RECOVERY_FRESH_AGE_MS,BOARD_RECOVERY_FRESH_AHEAD_MS);
}
uint8_t board_recovery_stamp_age(SedsRouter*router,uint64_t stamp,int64_t*age_ms){
 uint8_t kind=3U;int64_t age=0;if(!router)router=recovery_router;
 if(!router||!age_ms||!stamp||stamp==UINT64_MAX||
    seds_router_network_stamp_age_ms(router,board_stamp_epoch(stamp),board_stamp_ms(stamp),&kind,&age)!=SEDS_OK||kind!=0U)return 0U;
 *age_ms=age;return 1U;
}
/* Same checks as board_recovery_command_is_stale, naming the one that refused. */
uint8_t board_recovery_command_stale_reason(const SedsPacketView *packet)
{
 if (packet==0) return BOARD_REFUSAL_STALE;
 const uint64_t stamp=board_stamp_of(packet);
 if (!board_recovery_command_time_allowed(stamp)) return BOARD_REFUSAL_BELOW_FLOOR;
 if (board_recovery_floor_active() && !board_recovery_request_fresh(recovery_router,stamp)) return BOARD_REFUSAL_STALE;
 return BOARD_REFUSAL_NONE;
}
uint8_t board_recovery_command_is_stale(const SedsPacketView *packet)
{
 if (packet==0) return 1U;
 const uint64_t stamp=board_stamp_of(packet);
 if (!board_recovery_command_time_allowed(stamp)) return 1U;
 if (board_recovery_floor_active() && !board_recovery_request_fresh(recovery_router,stamp)) return 1U;
 return 0U;
}
uint8_t board_recovery_managed_update_allowed(const SedsPacketView *packet)
{
 if (!board_recovery_floor_is_healthy()) return 0U;
 if (!board_recovery_floor_active()) return packet!=NULL && packet->ty==SEDS_DT_FLIGHT_STATE ? recovery_router!=NULL && authorized_primary(recovery_router,packet) : 1U;
 return recovery_router!=0 && authorized_primary(recovery_router,packet) &&
        !board_recovery_command_is_stale(packet);
}
/* A well-formed FLIGHT_STATE whose sender is not the primary station is a
 * state report from another node, never a desired-state write: the cache
 * callback refuses it without counting a cache error. */
uint8_t board_recovery_managed_report_ignorable(const SedsPacketView *packet)
{
 return packet!=NULL && packet->ty==SEDS_DT_FLIGHT_STATE && recovery_router!=NULL &&
        packet->sender!=NULL && packet->sender_len!=0U &&
        !authorized_primary(recovery_router,packet);
}
uint8_t board_recovery_automatic_is_stale(const SedsPacketView *packet)
{
 return board_recovery_command_is_stale(packet);
}

/* Issuer of the last valid station QUERY, echoed by the Abort push. */
static uint64_t last_query_issuer;
static SedsResult send_status(SedsRouter*router,uint8_t result,const BoardRecoveryRequest*request,
                            uint64_t generation,const BoardRecoverySnapshot*snapshot,uint32_t uptime_ms) {
 uint8_t status[BOARD_RECOVERY_STATUS_SIZE];
 board_recovery_encode_status(status,result,snapshot->actual_state,(uint8_t)(snapshot->flags|BOARD_RECOVERY_VERIFIED_IMPLEMENTATION|BOARD_RECOVERY_MANUAL_UTC_IMPLEMENTATION|BOARD_RECOVERY_RESYNC_IMPLEMENTATION),request,
  generation,snapshot->abort_epoch,uptime_ms,snapshot->actual_outputs_mask);
 return seds_router_log_queue_typed(router,SEDS_DT_BOARD_STATE_STATUS,status,sizeof status,sizeof(uint8_t),SEDS_EK_UNSIGNED);
}
static bool bootstrap_fresh(void*context,uint64_t stamp) {
 return board_recovery_request_fresh((SedsRouter*)context,stamp)!=0U;
}
static bool bootstrap_reply(void*context,const BoardRecoveryRequest*request,uint8_t result) {
 BoardRecoverySnapshot snapshot={.actual_state=255U};(void)board_recovery_snapshot(&snapshot);
 return send_status((SedsRouter*)context,result,request,board_recovery_boot_current(),&snapshot,
  (uint32_t)(((uint64_t)tx_time_get()*1000U)/TX_TIMER_TICKS_PER_SECOND))==SEDS_OK;
}
static SedsResult handle_serialized(SedsRouter*router,const SedsPacketView*packet,uint32_t local_endpoint,uint32_t uptime_ms) {
 if(!router||!packet||packet->ty!=SEDS_DT_BOARD_RECOVERY_REQUEST||packet->num_endpoints!=1U||!packet->endpoints||packet->endpoints[0]!=local_endpoint)return SEDS_BAD_ARG;
 if(!authorized_primary(router,packet))return SEDS_PERMISSION_DENIED;
 BoardRecoveryRequest request;
 if(!board_recovery_decode_request(packet->payload,packet->payload_len,&request))return SEDS_BAD_ARG;
 const uint64_t stamp=board_stamp_of(packet);
 BoardRecoverySnapshot snapshot={.actual_state=255U};int32_t observed=board_recovery_snapshot(&snapshot);
 uint64_t generation=board_recovery_boot_current();uint8_t result=observed==0?BOARD_RECOVERY_RESULT_OK:BOARD_RECOVERY_RESULT_IO;
 if(request.operation==BOARD_RECOVERY_QUERY) {
  last_query_issuer=request.issuer_session;
  generation=board_bootstrap_storage_query_generation();
  if((!generation&&!board_bootstrap_storage_uncertain_first())||(snapshot.flags&BOARD_RECOVERY_SUPPORTED)==0U){snapshot.flags&=(uint8_t)~BOARD_RECOVERY_SUPPORTED;result=BOARD_RECOVERY_RESULT_UNSUPPORTED;}
  else if(generation==UINT64_MAX)result=BOARD_RECOVERY_RESULT_IO;
  else if(board_bootstrap_storage_pending()) {
   uint8_t required=BOARD_RECOVERY_ABORT_LATCHED|BOARD_RECOVERY_SAFE_OUTPUTS|BOARD_RECOVERY_OUTPUTS_VALID|BOARD_RECOVERY_SUPPORTED;
   result=board_bootstrap_storage_query_healthy()&&(snapshot.flags&required)==required?BOARD_RECOVERY_RESULT_OK:BOARD_RECOVERY_RESULT_IO;
  }
  /* Normal completed recovery needs a fresh QUERY too; active Launch cannot
   * arm it, and an old queued query never gains a renewed stamp. */
  if((result==BOARD_RECOVERY_RESULT_OK||board_bootstrap_storage_pending())&&
     bootstrap_fresh(router,stamp))
   (void)board_bootstrap_storage_note_query(&request,stamp,generation,snapshot.abort_epoch);
  return send_status(router,result,&request,generation,&snapshot,uptime_ms);
 }
 int32_t staged=board_bootstrap_storage_stage_first_leg(&request,stamp,snapshot.abort_epoch,router,bootstrap_reply,bootstrap_fresh);
 if(staged<0)return SEDS_OK;
 result=(uint8_t)staged;
 (void)board_recovery_snapshot(&snapshot);

 return send_status(router,result,&request,generation,&snapshot,uptime_ms);
}
/* One unsolicited status for a fresh Abort latch, so the station sees it even
 * while its ordered queries wait: nonce 0, the issuer of the last valid QUERY,
 * the QUERY reply's generation, the live latch flags and abort epoch. */
SedsResult board_recovery_push_abort_status(SedsRouter*router,uint32_t uptime_ms) {
 BoardRecoverySnapshot snapshot={.actual_state=255U};(void)board_recovery_snapshot(&snapshot);
 snapshot.flags|=BOARD_RECOVERY_ABORT_LATCHED;
 BoardRecoveryRequest request={0};request.issuer_session=last_query_issuer;
 return send_status(router,BOARD_RECOVERY_RESULT_OK,&request,board_bootstrap_storage_query_generation(),&snapshot,uptime_ms);
}
SedsResult board_recovery_handle(SedsRouter*router,const SedsPacketView*packet,uint32_t local_endpoint,uint32_t uptime_ms) {
 /* Nonblocking serialization: no transport waits/SDK lock cycle, and manual
  * Stop has its independent endpoint/owner path. */
 if(atomic_flag_test_and_set_explicit(&dispatching,memory_order_acquire))return SEDS_IO;
 SedsResult result=handle_serialized(router,packet,local_endpoint,uptime_ms);
 atomic_flag_clear_explicit(&dispatching,memory_order_release);return result;
}
