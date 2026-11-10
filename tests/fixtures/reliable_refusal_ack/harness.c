/* Real SDK router + actual AB endpoint handlers, managed FLIGHT_STATE owner,
 * fill settings and recovery transport. A delivered reliable packet that the
 * board deliberately refuses must still produce exactly one end-to-end
 * receipt, and must not be applied. Hardware, queues and storage are fakes. */
#define _POSIX_C_SOURCE 200809L
#include "sedsnet.h"
#include "sedsnet_config.h"
#include "tx_api.h"
#include "main.h"
#include "thread_comm.h"
#include "flight_state_cache.h"
#include "fill_settings.h"
#include "fill_motion.h"
#include "board_recovery.h"
#include "board_recovery_boot.h"
#include "board_recovery_floor.h"
#include "board_recovery_transport.h"
#include "board_recovery_wire.h"
#include "board_bootstrap_storage.h"
#include "bootstrap_marker.h"
#include "persistent_store.h"
#include "persistent_store_health.h"
#include "ground_heartbeat_owner.h"
#include "board_commands.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern volatile uint32_t g_flight_state_cache_errors,g_flight_state_cache_reports_ignored,g_flight_state_cache_refused_writes;

/* ---- ThreadX / HAL fakes ---- */
UINT tx_mutex_create(TX_MUTEX*m,const char*n,UINT i){(void)n;(void)i;m->locked=0;return TX_SUCCESS;}
UINT tx_mutex_get(TX_MUTEX*m,ULONG w){(void)w;m->locked++;return TX_SUCCESS;}
UINT tx_mutex_put(TX_MUTEX*m){m->locked--;return TX_SUCCESS;}
ULONG tx_time_get(void){return 1000U;}
uint32_t HAL_GetTick(void){return 1000U;}
volatile uint32_t g_telemetry_discovery_seen;

/* ---- board state fakes (not under test) ---- */
static bool stale,aborted;static UINT admit_result=TX_SUCCESS,apply_result=TX_SUCCESS;
static unsigned applied,admitted,abort_requests,stores;
uint8_t board_recovery_command_time_allowed(uint64_t t){(void)t;return !stale;}
uint8_t board_recovery_floor_active(void){return 0U;}
uint8_t board_recovery_floor_is_healthy(void){return 1U;}
uint8_t board_recovery_floor_read_cutoff(uint64_t*c){*c=0U;return 1U;}
bool bootstrap_request_fresh(uint64_t s,uint64_t n){(void)s;(void)n;return true;}
UINT thread_comm_publish_command_cutoff(uint64_t c){(void)c;return TX_SUCCESS;}
uint8_t thread_comm_get_abort(void){return aborted;}
UINT thread_comm_apply_network_flight_state(uint8_t s,uint64_t t){(void)s;(void)t;if(apply_result==TX_SUCCESS)applied++;return apply_result;}
UINT thread_comm_admit_network(thread_comm_msg_t m,uint64_t t){(void)m;(void)t;if(admit_result==TX_SUCCESS)admitted++;return admit_result;}
UINT thread_comm_admit_network_automatic(thread_comm_msg_t m,uint64_t t){(void)m;(void)t;if(admit_result==TX_SUCCESS)admitted++;return admit_result;}
UINT thread_comm_admit_network_launch_original_at(thread_comm_msg_t m,const char*s,uint32_t l,uint64_t u,uint32_t k,uint32_t(*n)(void)){(void)m;(void)s;(void)l;(void)u;(void)k;(void)n;admitted++;return TX_SUCCESS;}
uint8_t thread_comm_launch_original_accepted(uint64_t t,const char*s,uint32_t l){(void)t;(void)s;(void)l;return 0U;}
uint8_t thread_comm_request_automatic_abort(uint8_t o){(void)o;abort_requests++;return 1U;}
UINT thread_comm_request_manual_abort(void){abort_requests++;return 1U;}
UINT thread_comm_request_manual_abort_original_utc(uint64_t t){(void)t;abort_requests++;return 1U;}
bool thread_comm_fill_config_begin(uint32_t e,uint8_t(*p)(void*),void*c){(void)e;(void)p;(void)c;return false;}
bool thread_comm_fill_config_end(uint32_t e,void(*p)(void*),void*c){(void)e;(void)p;(void)c;return false;}
int32_t board_recovery_snapshot(BoardRecoverySnapshot*o){memset(o,0,sizeof*o);o->abort_epoch=1U;o->flags=BOARD_RECOVERY_SUPPORTED;return 0;}
uint64_t board_recovery_boot_current(void){return 7U;}
uint64_t board_bootstrap_storage_query_generation(void){return 7U;}
bool board_bootstrap_storage_pending(void){return false;}
bool board_bootstrap_storage_uncertain_first(void){return false;}
bool board_bootstrap_storage_query_healthy(void){return true;}
bool board_bootstrap_storage_note_query(const BoardRecoveryRequest*r,uint64_t a,uint64_t b,uint32_t c){(void)r;(void)a;(void)b;(void)c;return true;}
int32_t board_bootstrap_storage_stage_first_leg(const BoardRecoveryRequest*r,uint64_t a,uint64_t b,uint32_t c,void*d,BootstrapReply e,BootstrapNow f){(void)r;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -1;}
bool persistent_store_controls_ready(void){return true;}
bool persistent_store_bootstrap_pending(void){return false;}
void persistent_store_note_failure(void){}
bool persistent_store_enter(void){return false;}
bool persistent_store_leave(void){return true;}
launchcore_persist_status_t persistent_store_init(void){return LAUNCHCORE_PERSIST_OK;}
launchcore_persist_status_t persistent_store_get(uint32_t k,void*p,size_t*n){(void)k;(void)p;(void)n;return LAUNCHCORE_PERSIST_NOT_FOUND;}
launchcore_persist_status_t persistent_store_set(uint32_t k,const void*p,size_t n){(void)k;(void)p;(void)n;stores++;return LAUNCHCORE_PERSIST_OK;}
launchcore_persist_status_t persistent_store_try_set(uint32_t k,const void*p,size_t n,bool*b){*b=false;return persistent_store_set(k,p,n);}
launchcore_persist_status_t launchcore_persist_get_strict(uint32_t k,void*p,size_t*n,launchcore_persist_strict_info_t*i){(void)k;(void)p;(void)n;memset(i,0,sizeof*i);return LAUNCHCORE_PERSIST_STORAGE_ERROR;}
launchcore_persist_status_t launchcore_persist_set_strict(uint32_t k,const void*p,size_t n,launchcore_persist_strict_info_t*i){(void)k;(void)p;(void)n;memset(i,0,sizeof*i);return LAUNCHCORE_PERSIST_STORAGE_ERROR;}
uint8_t main_task_fill_prepare_mode(uint8_t m){return m;}
void main_task_fill_mode_changed(void){}
bool main_task_fill_snapshot(uint8_t*m,uint8_t*a,uint8_t*b,uint32_t*r){*m=0;*a=0;*b=0;*r=0;return true;}
bool main_task_fill_snapshot_v2(FillMotionSnapshotV2*s){memset(s,0,sizeof*s);s->current_epoch=1U;return true;}
static SedsResult Heartbeat_handler(const SedsPacketView*p,void*u){(void)p;(void)u;return SEDS_OK;}

/* ---- actual production handlers from Core/Src/telemetry.c ---- */
static struct {SedsRouter*r;} g_router;
volatile uint32_t g_command_queue_drop_count,g_flight_state_handler_count,g_flight_state_handler_error_count;
volatile uint8_t g_last_flight_state_packet;
@@HANDLERS@@

/* ---- SDK wiring ---- */
static unsigned acks,status_frames;
static SedsResult ab_side_tx(const SedsPacketView*p,void*u){(void)u;
 if(p->sender_len>=7&&!memcmp(p->sender,"E2EACK:",7))acks++;
 if(p->ty==SEDS_DT_AB_FILL_DRIVE_STATUS||p->ty==SEDS_DT_AB_FILL_MOTION_STATUS_V2||p->ty==SEDS_DT_BOARD_STATE_STATUS)status_frames++;
 return SEDS_OK;}
typedef struct{SedsRouter*receiver;uint32_t side;}Target;
static SedsResult learn(const SedsPacketView*p,void*u){Target*t=u;return seds_router_receive_from_side(t->receiver,t->side,p);}
static uint64_t now_ms(void*u){(void)u;return 5000U;}
static SedsRouter*ab;static int32_t sid;
static void learn_station(void){
 SedsLocalEndpointDesc gs_local={.endpoint=SEDS_EP_GROUND_STATION,.packet_handler=Heartbeat_handler};
 SedsRouter*gs=seds_router_new(now_ms,NULL,&gs_local,1);assert(gs);assert(seds_router_set_sender_id(gs,"GS",2)==SEDS_OK);
 Target t={ab,(uint32_t)sid};assert(seds_router_add_side_packet(gs,"link",4,learn,&t,false)>=0);
 bool queued=false;assert(seds_router_poll_discovery(gs,&queued)==SEDS_OK);assert(seds_router_process_all_queues(gs)==SEDS_OK);
 (void)seds_router_process_all_queues(ab);seds_router_free(gs);
}
static uint64_t stamp=1767225600000ULL;
/* Deliver one fresh reliable packet; return the end-to-end receipts it earned. */
static unsigned deliver(uint32_t ty,uint32_t endpoint,const char*sender,const uint8_t*payload,size_t n,SedsResult*rx){
 SedsPacketView p={.ty=ty,.data_size=n,.sender=sender,.sender_len=strlen(sender),.endpoints=&endpoint,.num_endpoints=1,.timestamp=++stamp,.payload=payload,.payload_len=n};
 unsigned before=acks;SedsResult r=seds_router_receive_from_side(ab,(uint32_t)sid,&p);(void)seds_router_process_all_queues(ab);
 if(rx)*rx=r;
 return acks-before;
}
static void fresh_case(void){stale=aborted=false;admit_result=apply_result=TX_SUCCESS;applied=admitted=abort_requests=stores=0;status_frames=0;}

int main(int argc,char**argv){
 assert(argc==3);setbuf(stdout,NULL);
 assert(seds_schema_register_json_file(argv[1],strlen(argv[1]))==SEDS_OK);
 const SedsLocalEndpointDesc locals[]={
  {.endpoint=SEDS_EP_BOARD_RECOVERY,.packet_handler=Board_Recovery_handler},
  {.endpoint=SEDS_EP_ACTUATOR_BOARD,.packet_handler=control_endpoint},
  {.endpoint=SEDS_EP_ABORT,.packet_handler=Abort_handler},
  {.endpoint=SEDS_EP_FLIGHT_STATE,.packet_handler=flight_state_endpoint},
  {.endpoint=SEDS_EP_HEART_BEAT,.packet_handler=Heartbeat_handler}};
 ab=seds_router_new(now_ms,NULL,locals,sizeof locals/sizeof locals[0]);assert(ab);
 assert(seds_router_set_sender_id(ab,"AB",2)==SEDS_OK);
 sid=seds_router_add_side_packet(ab,"can",3,ab_side_tx,NULL,false);assert(sid>=0);
 g_router.r=ab;board_recovery_transport_set_router(ab);
 assert(flight_state_cache_init(ab)==SEDS_OK);fill_settings_restore();
 learn_station();
 const char*c=argv[2];SedsResult rx=SEDS_OK;unsigned got;fresh_case();
 uint32_t refusals=g_reliable_refusal_count;
 if(!strcmp(c,"fstate_stale")){
  /* A refused station write is acknowledged and counted as a refusal, not a cache error. */
  const uint32_t errors_before=g_flight_state_cache_errors,refused_before=g_flight_state_cache_refused_writes;
  uint8_t s=2;stale=true;got=deliver(SEDS_DT_FLIGHT_STATE,SEDS_EP_FLIGHT_STATE,"GS",&s,1,&rx);
  printf("stale FLIGHT_STATE rx=%d acks=%u applied=%u stores=%u\n",rx,got,applied,stores);
  assert(got==1&&rx==SEDS_OK&&applied==0&&stores==0&&!flight_state_cache_has_value());
  assert(g_flight_state_cache_errors==errors_before&&g_flight_state_cache_refused_writes>refused_before);
 }else if(!strcmp(c,"fstate_foreign")){
  /* Managed-owner authority refuses a non-station write before any apply. */
  /* A report from another node is a correct refusal, not a cache error. */
  const uint32_t errors=g_flight_state_cache_errors,ignored=g_flight_state_cache_reports_ignored;
  uint8_t s=2;got=deliver(SEDS_DT_FLIGHT_STATE,SEDS_EP_FLIGHT_STATE,"VB",&s,1,&rx);
  printf("foreign FLIGHT_STATE rx=%d acks=%u applied=%u stores=%u errors=%u ignored=%u\n",rx,got,applied,stores,
         (unsigned)(g_flight_state_cache_errors-errors),(unsigned)(g_flight_state_cache_reports_ignored-ignored));
  assert(got==1&&rx==SEDS_OK&&applied==0&&stores==0&&!flight_state_cache_has_value());
  assert(g_flight_state_cache_errors==errors&&g_flight_state_cache_reports_ignored>ignored);
 }else if(!strcmp(c,"fstate_aborted")){
  /* A refused station write is acknowledged and counted as a refusal, not a cache error. */
  const uint32_t errors_before=g_flight_state_cache_errors,refused_before=g_flight_state_cache_refused_writes;
  uint8_t s=3;aborted=true;got=deliver(SEDS_DT_FLIGHT_STATE,SEDS_EP_FLIGHT_STATE,"GS",&s,1,&rx);
  printf("aborted FLIGHT_STATE rx=%d acks=%u applied=%u stores=%u\n",rx,got,applied,stores);
  assert(got==1&&rx==SEDS_OK&&applied==0&&stores==0&&!flight_state_cache_has_value());
  assert(g_flight_state_cache_errors==errors_before&&g_flight_state_cache_refused_writes>refused_before);
 }else if(!strcmp(c,"fstate_late")){
  /* A refused station write is acknowledged and counted as a refusal, not a cache error. */
  const uint32_t errors_before=g_flight_state_cache_errors,refused_before=g_flight_state_cache_refused_writes;
  uint8_t s=4;apply_result=THREAD_COMM_STALE_TIMESTAMP;got=deliver(SEDS_DT_FLIGHT_STATE,SEDS_EP_FLIGHT_STATE,"GS",&s,1,&rx);
  printf("late FLIGHT_STATE rx=%d acks=%u applied=%u stores=%u\n",rx,got,applied,stores);
  assert(got==1&&rx==SEDS_OK&&applied==0&&stores==0&&!flight_state_cache_has_value());
  assert(g_flight_state_cache_errors==errors_before&&g_flight_state_cache_refused_writes>refused_before);
 }else if(!strcmp(c,"fstate_transient")){
  /* A transient owner refusal is still retried: no receipt, nothing stored. */
  const uint32_t transient_errors=g_flight_state_cache_errors;
  uint8_t s=4;apply_result=TX_MUTEX_ERROR;got=deliver(SEDS_DT_FLIGHT_STATE,SEDS_EP_FLIGHT_STATE,"GS",&s,1,&rx);
  printf("transient FLIGHT_STATE rx=%d acks=%u stores=%u\n",rx,got,stores);
  assert(got==0&&rx!=SEDS_OK&&stores==0);refusals=g_reliable_refusal_count;
  assert(g_flight_state_cache_errors>transient_errors);
 }else if(!strcmp(c,"fstate_applied")){
  uint8_t s=1;got=deliver(SEDS_DT_FLIGHT_STATE,SEDS_EP_FLIGHT_STATE,"GS",&s,1,&rx);
  printf("accepted FLIGHT_STATE rx=%d acks=%u applied=%u stores=%u\n",rx,got,applied,stores);
  assert(got==1&&rx==SEDS_OK&&applied>=1&&stores==1&&flight_state_cache_value()==1);refusals=g_reliable_refusal_count;
 }else if(!strcmp(c,"fill_query")){
  uint8_t q[32]={1};q[4]=1;q[24]=1;got=deliver(SEDS_DT_AB_FILL_DRIVE_SETTINGS,SEDS_EP_ACTUATOR_BOARD,"GS",q,32,&rx);
  printf("GS fill QUERY rx=%d acks=%u status=%u\n",rx,got,status_frames);
  assert(got==1&&rx==SEDS_OK&&status_frames==1);refusals=g_reliable_refusal_count;
 }else if(!strcmp(c,"fill_foreign")){
  uint8_t q[32]={1};q[4]=1;q[24]=1;got=deliver(SEDS_DT_AB_FILL_DRIVE_SETTINGS,SEDS_EP_ACTUATOR_BOARD,"VB",q,32,&rx);
  printf("foreign fill request rx=%d acks=%u status=%u\n",rx,got,status_frames);
  assert(got==1&&rx==SEDS_OK&&status_frames==0);
 }else if(!strcmp(c,"fill_malformed")){
  uint8_t q[32]={9};q[4]=1;q[24]=1;got=deliver(SEDS_DT_AB_FILL_DRIVE_SETTINGS,SEDS_EP_ACTUATOR_BOARD,"GS",q,32,&rx);
  printf("malformed fill request rx=%d acks=%u status=%u\n",rx,got,status_frames);
  assert(got==1&&rx==SEDS_OK&&status_frames==0);
 }else if(!strcmp(c,"command_malformed")){
  uint8_t cmd=0x80U|0x7FU;got=deliver(SEDS_DT_ACTUATOR_COMMAND,SEDS_EP_ACTUATOR_BOARD,"GS",&cmd,1,&rx);
  printf("unknown automatic command rx=%d acks=%u admitted=%u\n",rx,got,admitted);
  assert(got==1&&rx==SEDS_OK&&admitted==0);
 }else if(!strcmp(c,"command_queue_full")){
  /* Safety refusal stays a refusal; a full queue is still retried. */
  uint8_t cmd=0;admit_result=TX_QUEUE_ERROR;got=deliver(SEDS_DT_ACTUATOR_COMMAND,SEDS_EP_ACTUATOR_BOARD,"GS",&cmd,1,&rx);
  printf("queue-full command rx=%d acks=%u admitted=%u\n",rx,got,admitted);
  assert(got==0&&rx!=SEDS_OK&&admitted==0);refusals=g_reliable_refusal_count;
 }else if(!strcmp(c,"recovery_foreign")){
  uint8_t req[BOARD_RECOVERY_REQUEST_SIZE]={0};got=deliver(SEDS_DT_BOARD_RECOVERY_REQUEST,SEDS_EP_BOARD_RECOVERY,"VB",req,sizeof req,&rx);
  printf("foreign recovery request rx=%d acks=%u status=%u\n",rx,got,status_frames);
  assert(got==1&&rx==SEDS_OK&&status_frames==0);
 }else if(!strcmp(c,"recovery_malformed")){
  uint8_t req[BOARD_RECOVERY_REQUEST_SIZE];memset(req,0xA5,sizeof req);got=deliver(SEDS_DT_BOARD_RECOVERY_REQUEST,SEDS_EP_BOARD_RECOVERY,"GS",req,sizeof req,&rx);
  printf("malformed recovery request rx=%d acks=%u status=%u\n",rx,got,status_frames);
  assert(got==1&&rx==SEDS_OK&&status_frames==0);
 }else if(!strcmp(c,"abort")){
  const char m[]="Manual Abort Command Issued";uint32_t eps[]={SEDS_EP_GROUND_STATION,SEDS_EP_FLIGHT_CONTROLLER,SEDS_EP_VALVE_BOARD,SEDS_EP_ACTUATOR_BOARD,SEDS_EP_ABORT,SEDS_EP_FLIGHT_STATE,SEDS_EP_SD_CARD};
  SedsPacketView p={.ty=SEDS_DT_ABORT,.data_size=sizeof m-1,.sender="GS",.sender_len=2,.endpoints=eps,.num_endpoints=7,.timestamp=++stamp,.payload=(const uint8_t*)m,.payload_len=sizeof m-1};
  unsigned before=acks;rx=seds_router_receive_from_side(ab,(uint32_t)sid,&p);(void)seds_router_process_all_queues(ab);got=acks-before;
  printf("station ABORT fan-out rx=%d acks=%u abort_requests=%u\n",rx,got,abort_requests);
  assert(got==1&&rx==SEDS_OK&&abort_requests==1);refusals=g_reliable_refusal_count;
 }else{fprintf(stderr,"unknown case %s\n",c);return 2;}
 /* Every acknowledged refusal is visible to the operator probe. */
 printf("refusals=%u\n",(unsigned)(g_reliable_refusal_count-refusals));
 assert(g_reliable_refusal_count-refusals>=(strstr(c,"applied")||strstr(c,"query")||strstr(c,"transient")||strstr(c,"queue_full")||!strcmp(c,"abort")?0U:1U));
 seds_router_free(ab);printf("PASS %s\n",c);return 0;
}
