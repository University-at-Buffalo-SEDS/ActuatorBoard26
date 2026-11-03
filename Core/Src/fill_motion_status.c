#include "fill_motion_status.h"
#include "fill_motion.h"
void fill_motion_provenance_start(FillMotionProvenance*p,uint64_t source,uint32_t epoch,uint8_t action,uint32_t tick){
 *p=(FillMotionProvenance){.source=source,.started_ms=tick,.origin_epoch=epoch,.action=action};
 if(source&&source!=UINT64_MAX&&epoch&&epoch!=UINT32_MAX&&(action==FILL_ACTION_RETRACT||action==FILL_ACTION_EXTEND))p->lifecycle=FILL_PROVENANCE_ACTIVE;
}
void fill_motion_provenance_complete(FillMotionProvenance*p){if(p->lifecycle==FILL_PROVENANCE_ACTIVE)p->lifecycle=FILL_PROVENANCE_COMPLETED;}
void fill_motion_provenance_cancel(FillMotionProvenance*p,bool fault){
 if(fault)p->lifecycle=FILL_PROVENANCE_FAULT;
 else if(p->lifecycle!=FILL_PROVENANCE_UNKNOWN)p->lifecycle=FILL_PROVENANCE_CANCELLED;
}
uint32_t fill_motion_provenance_remaining(FillMotionProvenance*p,uint32_t tick){
 if(p->lifecycle!=FILL_PROVENANCE_ACTIVE&&p->lifecycle!=FILL_PROVENANCE_COMPLETED)return FILL_DURATION_MS;
 uint32_t elapsed=tick-p->started_ms;
 /* Owner conservatively anchors at the next tick boundary. A query in the
  * acceptance tick must not interpret that small future offset as wrap age. */
 if(!p->minimum_satisfied&&(int32_t)elapsed<0)return FILL_DURATION_MS;
 if(!p->minimum_satisfied&&elapsed>=FILL_DURATION_MS)p->minimum_satisfied=true;
 return p->minimum_satisfied?0U:FILL_DURATION_MS-elapsed;
}
static void write32(uint8_t*p,uint32_t value){for(unsigned i=0;i<4U;i++)p[i]=(uint8_t)(value>>(8U*i));}
static void write64(uint8_t*p,uint64_t value){for(unsigned i=0;i<8U;i++)p[i]=(uint8_t)(value>>(8U*i));}
void fill_motion_status_v2_encode(uint8_t out[56],const uint8_t request[32],uint8_t result,uint64_t generation,const FillMotionSnapshotV2*s){
 out[0]=2U;out[1]=result;out[2]=s->mode;out[3]=5U;
 for(unsigned i=0;i<8U;i++){out[4U+i]=request[4U+i];out[24U+i]=request[24U+i];}
 write64(out+12,generation);write32(out+20,s->current_epoch);
 out[32]=s->active_action;out[33]=s->busy;out[34]=s->lifecycle;out[35]=s->accepted_action;
 write32(out+36,s->remaining_ms);write64(out+40,s->accepted_source);write32(out+48,s->minimum_remaining_ms);write32(out+52,s->origin_epoch);
}
