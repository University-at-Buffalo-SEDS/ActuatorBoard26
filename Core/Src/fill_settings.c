#include "fill_settings.h"
#include "fill_motion.h"
#include "fill_owner_gate.h"
#include "persistent_store.h"
#include "persistent_store_health.h"
#include "board_recovery.h"
#include "board_recovery_boot.h"
#include "board_recovery_floor.h"
#include "launchcore/persist.h"
#include "tx_api.h"
#include <stdatomic.h>
#include <string.h>
#define FILL_SETTINGS_KEY UINT32_C(0x46524456) /* App-owned FDRV; not SDK metadata. */
static _Atomic uint8_t selected_mode=FILL_MODE_UNKNOWN;
static _Atomic bool selected_ready;
/* Boot classification only. This permits a fresh explicit main-owner SET to
 * reconcile a truly absent FDRV after critical commissioning, never QUERY. */
static _Atomic bool absent_candidate;
/* Only main owner reads/writes this64-bit value, including its bounded callbacks. */
static uint64_t selected_cutoff;
#ifdef TELEMETRY_ENABLED
static _Atomic bool reconcile_pending;
static uint8_t reconcile_mode;
static uint64_t reconcile_source;
#endif
static uint64_t decode64(const uint8_t*p){uint64_t v=0U;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
#ifdef TELEMETRY_ENABLED
static void encode64(uint8_t*p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
#endif
static bool decode_record(const uint8_t*p,size_t n,uint8_t*mode,uint64_t*source){
 if(n!=16U||p[0]!=1U||p[1]>2U)return false;
 for(unsigned i=2;i<8;i++)if(p[i]!=0U)return false;
 uint64_t time=decode64(p+8);if(!time||time==UINT64_MAX)return false;
 *mode=p[1];*source=time;return true;
}
uint8_t fill_settings_mode(void){return selected_mode;}
bool fill_settings_ready(void){return selected_ready;}
uint64_t fill_settings_cutoff(void){return selected_cutoff;}
#ifdef TELEMETRY_ENABLED
#include "sedsnet_config.h"
static TX_MUTEX settings_mutex;
static _Atomic bool settings_initialized;
static struct {uint8_t bytes[32],result;uint64_t source;bool valid,pending,dirty;SedsRouter*router;} cached;
volatile uint32_t g_fill_settings_status_errors;
static bool retry_waiting;
static uint32_t retry_started;
static bool cache_enter(void){
 if(!settings_initialized)return false;
 if(tx_mutex_get(&settings_mutex,TX_WAIT_FOREVER)==TX_SUCCESS)return true;
 selected_ready=false;settings_initialized=false;return false;
}
static bool cache_leave(void){if(tx_mutex_put(&settings_mutex)==TX_SUCCESS)return true;selected_ready=false;settings_initialized=false;return false;}
#endif
void fill_settings_restore(void){
 selected_ready=false;selected_mode=FILL_MODE_UNKNOWN;selected_cutoff=0U;absent_candidate=false;
#ifdef TELEMETRY_ENABLED
 reconcile_pending=false;
 if(tx_mutex_create(&settings_mutex,"fill_settings",TX_INHERIT)!=TX_SUCCESS)return;
 settings_initialized=true;
#endif
 if(!persistent_store_enter())return;
 uint8_t bytes[16];size_t n=sizeof bytes;launchcore_persist_strict_info_t info;
 launchcore_persist_status_t status=launchcore_persist_get_strict(FILL_SETTINGS_KEY,bytes,&n,&info);
 uint8_t mode=FILL_MODE_UNKNOWN;uint64_t source=0U;
 bool valid=status==LAUNCHCORE_PERSIST_OK&&decode_record(bytes,n,&mode,&source);
 if(status==LAUNCHCORE_PERSIST_NOT_FOUND&&info.health==LAUNCHCORE_PERSIST_HEALTH_HEALTHY&&info.record==LAUNCHCORE_PERSIST_RECORD_ABSENT){mode=FILL_MODE_STEPPER;valid=true;}
 bool absent=status==LAUNCHCORE_PERSIST_NOT_FOUND&&info.record==LAUNCHCORE_PERSIST_RECORD_ABSENT&&
  (info.health==LAUNCHCORE_PERSIST_HEALTH_HEALTHY||info.health==LAUNCHCORE_PERSIST_HEALTH_VIRGIN);
 bool released=persistent_store_leave();
 if(released&&absent)absent_candidate=true;
 if(status==LAUNCHCORE_PERSIST_STORAGE_ERROR||status==LAUNCHCORE_PERSIST_CORRUPT)persistent_store_note_failure();
 if(valid&&released){selected_cutoff=source;selected_mode=mode;selected_ready=true;}
}
#ifdef TELEMETRY_ENABLED
static bool fresh(uint64_t source,uint64_t now){return source&&source!=UINT64_MAX&&now&&now!=UINT64_MAX&&(now>=source?now-source<=5000U:source-now<=1000U);}
static uint32_t decode32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void encode32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static SedsResult emit_motion_v2(SedsRouter*r,const uint8_t request[32],uint8_t result){
 BoardRecoverySnapshot board={0};int32_t status=board_recovery_snapshot(&board);FillMotionSnapshotV2 motion={0};bool valid=main_task_fill_snapshot_v2(&motion);
 if(!valid||status!=0||motion.current_epoch!=board.abort_epoch||!persistent_store_controls_ready()||!selected_ready)result=6U;
 if(persistent_store_bootstrap_pending())result=5U;
 uint8_t response[FILL_MOTION_STATUS_V2_SIZE];fill_motion_status_v2_encode(response,request,result,board_recovery_boot_current(),&motion);
 return seds_router_log_queue_typed(r,SEDS_DT_AB_FILL_MOTION_STATUS_V2,response,FILL_MOTION_STATUS_V2_SIZE,1U,SEDS_EK_UNSIGNED);
}
static SedsResult emit(SedsRouter*r,const uint8_t request[32],uint8_t result){
 if(request[0]==FILL_MOTION_QUERY_VERSION)return emit_motion_v2(r,request,result);
 BoardRecoverySnapshot snapshot={0};int32_t snap_status=board_recovery_snapshot(&snapshot);
 uint8_t response[40]={1U,result,selected_ready?selected_mode:FILL_MODE_UNKNOWN,5U};
 if((snap_status!=0||!persistent_store_controls_ready()||!selected_ready)&&result==0U)response[1]=6U;
 memcpy(response+4,request+4,8);encode64(response+12,board_recovery_boot_current());encode32(response+20,snapshot.abort_epoch);memcpy(response+24,request+24,8);
 uint32_t remaining=0U;
 if(!main_task_fill_snapshot(response+2,response+32,response+33,&remaining))response[1]=6U;
 encode32(response+36,remaining);
 /* RAM-only critical commissioning barrier overrides retryable FDRV IO. */
 if(persistent_store_bootstrap_pending())response[1]=5U;
 return seds_router_log_queue_typed(r,SEDS_DT_AB_FILL_DRIVE_STATUS,response,40U,1U,SEDS_EK_UNSIGNED);
}
SedsResult fill_settings_handle(SedsRouter*r,const SedsPacketView*p){
 if(!r||!p||p->ty!=SEDS_DT_AB_FILL_DRIVE_SETTINGS||p->num_endpoints!=1U||!p->endpoints||p->endpoints[0]!=SEDS_EP_ACTUATOR_BOARD)return SEDS_BAD_ARG;
 if(!seds_router_sender_matches_hostname(r,p->sender,p->sender_len,"GS",2U))return SEDS_PERMISSION_DENIED;
 if(p->payload_len!=32U||!p->payload)return SEDS_BAD_ARG;
 const uint8_t*q=p->payload;
 if((q[0]!=1U&&q[0]!=FILL_MOTION_QUERY_VERSION)||q[1]>1U||(q[0]==FILL_MOTION_QUERY_VERSION&&q[1]!=0U)||q[2]>2U||q[3]!=0U||!decode64(q+4)||!decode64(q+24)||(q[1]==0U&&q[2]!=0U))return SEDS_BAD_ARG;
 if(persistent_store_bootstrap_pending())return emit(r,q,5U);
 if(q[1]==0U)return emit(r,q,0U); /* QUERY does not read/write journal or alter holding torque. */
 if(!cache_enter())return SEDS_IO;
 bool duplicate=cached.valid&&!memcmp(q+4,cached.bytes+4,8)&&!memcmp(q+24,cached.bytes+24,8);
 uint8_t result=0U;
 if(duplicate)result=memcmp(q,cached.bytes,32)||p->timestamp!=cached.source?7U:cached.result;
 else if(cached.pending)result=5U;
 bool released=cache_leave();if(!released)return SEDS_IO;
 if(duplicate||result)return emit(r,q,result);
 if(q[2]==FILL_MODE_BRIDGE)return emit(r,q,2U); /* Uncorrected schematic: never selectable live. */
 uint64_t now=0U;BoardRecoverySnapshot snapshot={0};
 if(!decode64(q+12)||decode64(q+12)==UINT64_MAX||!decode32(q+20)||decode32(q+20)==UINT32_MAX)return emit(r,q,3U);
 if(seds_router_get_network_time_ms(r,&now)!=SEDS_OK||!fresh(p->timestamp,now))return emit(r,q,4U);
 if((!selected_ready&&!reconcile_pending&&!absent_candidate)||!persistent_store_controls_ready()||board_recovery_snapshot(&snapshot)!=0)return emit(r,q,6U);
 if(decode64(q+12)!=board_recovery_boot_current()||decode32(q+20)!=snapshot.abort_epoch)return emit(r,q,4U);
 if(snapshot.actual_state!=1U)return emit(r,q,5U);
 if(!cache_enter())return SEDS_IO;
 if(cached.pending){result=5U;}
 else {memcpy(cached.bytes,q,32);cached.source=p->timestamp;cached.router=r;cached.result=1U;cached.valid=true;cached.pending=true;cached.dirty=false;result=1U;}
 if(!cache_leave())return SEDS_IO;
 return emit(r,q,result);
}
static uint8_t prepare_mode(void*context){return main_task_fill_prepare_mode(*(uint8_t*)context);}
typedef struct {uint8_t mode;uint64_t source;} Publish;
static void publish_mode(void*context){Publish*p=context;selected_cutoff=p->source;selected_mode=p->mode;selected_ready=true;reconcile_pending=false;absent_candidate=false;main_task_fill_mode_changed();}
static bool store_record(uint8_t mode,uint64_t source,bool initialize_absent){
 if(!persistent_store_enter())return false;
 /* Consume before any read/write attempt; failed/uncertain IO cannot become
  * permission to reinterpret a present or damaged record as absent. */
 absent_candidate=false;
 uint8_t before[16],next[16]={1U,mode},observed[16];size_t n=sizeof before;launchcore_persist_strict_info_t info;
 launchcore_persist_status_t status=launchcore_persist_get_strict(FILL_SETTINGS_KEY,before,&n,&info);
 uint8_t old_mode=FILL_MODE_UNKNOWN;uint64_t old_source=0U;
 bool valid=status==LAUNCHCORE_PERSIST_OK&&decode_record(before,n,&old_mode,&old_source);
 bool absent=status==LAUNCHCORE_PERSIST_NOT_FOUND&&info.health==LAUNCHCORE_PERSIST_HEALTH_HEALTHY&&info.record==LAUNCHCORE_PERSIST_RECORD_ABSENT;
 if(absent){valid=true;old_mode=0U;}
 if(initialize_absent)valid=absent; /* Never overwrite an unknown PRESENT or tombstone. */
 uint8_t expected_mode=initialize_absent?0U:(reconcile_pending?reconcile_mode:selected_mode);
 valid=valid&&old_mode==expected_mode&&old_source==(reconcile_pending?reconcile_source:selected_cutoff)&&source>old_source;
 if(valid){encode64(next+8,source);status=launchcore_persist_set_strict(FILL_SETTINGS_KEY,next,sizeof next,&info);n=sizeof observed;if(status==LAUNCHCORE_PERSIST_OK){status=launchcore_persist_get_strict(FILL_SETTINGS_KEY,observed,&n,&info);valid=status==LAUNCHCORE_PERSIST_OK&&n==sizeof next&&!memcmp(next,observed,sizeof next);}else valid=false;}
 bool released=persistent_store_leave();
 if(!valid||!released){persistent_store_note_failure();selected_ready=false;return false;}
 return true;
}
void fill_settings_service(void){
 if(!settings_initialized)return;
 uint8_t request[32];uint64_t source=0U;SedsRouter*router=0;bool pending=false;
 if(!cache_enter())return;
 if(cached.valid&&cached.pending){memcpy(request,cached.bytes,32);source=cached.source;router=cached.router;pending=true;}
 if(!cache_leave())return;
 if(pending){
  bool initialize_absent=!selected_ready&&!reconcile_pending&&absent_candidate;
  uint8_t mode=request[2],result=0U;uint32_t epoch=decode32(request+20);uint64_t now=0U;
  if(persistent_store_bootstrap_pending())result=5U;
  else if((!selected_ready&&!reconcile_pending&&!absent_candidate)||!persistent_store_controls_ready())result=6U;
  else if(decode64(request+12)!=board_recovery_boot_current()||source<=(reconcile_pending?reconcile_source:selected_cutoff)||seds_router_get_network_time_ms(router,&now)!=SEDS_OK||!fresh(source,now))result=4U;
  else if(initialize_absent&&!board_recovery_command_time_allowed(source))result=4U;
  else if(!thread_comm_fill_config_begin(epoch,prepare_mode,&mode))result=5U;
  else {
   bool stored=store_record(mode,source,initialize_absent);Publish publish={mode,source};
   bool applied=thread_comm_fill_config_end(epoch,stored?publish_mode:0,&publish);
   if(!stored)result=6U;else if(!applied){selected_ready=false;reconcile_mode=mode;reconcile_source=source;reconcile_pending=true;result=4U;}
  }
  if(!cache_enter())return;
  cached.result=result;cached.pending=false;cached.dirty=true;retry_waiting=false;
  if(!cache_leave())return;
 }
 bool dirty=false;uint8_t result=0;
 if(!cache_enter())return;
 if(cached.valid&&cached.dirty){memcpy(request,cached.bytes,32);router=cached.router;result=cached.result;dirty=true;}
 if(!cache_leave())return;
 if(dirty){
  uint32_t ticks=(uint32_t)tx_time_get();
  if(retry_waiting&&(uint32_t)(ticks-retry_started)<(TX_TIMER_TICKS_PER_SECOND+9U)/10U)return;
  SedsResult sent=emit(router,request,result);
  if(sent!=SEDS_OK){++g_fill_settings_status_errors;retry_waiting=true;retry_started=ticks;return;}
  retry_waiting=false;
  if(!cache_enter())return;
  if(!memcmp(request,cached.bytes,32))cached.dirty=false;
  (void)cache_leave();
 }
}
#else
void fill_settings_service(void){}
#endif
