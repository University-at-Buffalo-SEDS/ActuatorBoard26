#include "fill_motion.h"
bool fill_source_fresh(uint64_t source,uint64_t now){
 return source!=0U&&source!=UINT64_MAX&&now!=0U&&now!=UINT64_MAX&&(now>=source?now-source<=5000U:source-now<=1000U);
}
bool fill_source_after(uint64_t source,uint64_t mark,uint64_t now){
 if(source>mark)return true;
 return mark>now&&mark-now>FILL_STATION_STEP_BACK_MS&&fill_source_fresh(source,now);
}
uint8_t fill_motion_start(FillMotion *m,uint8_t mode,uint8_t action,uint64_t source,uint64_t now,uint32_t tick){
 /* Bridge physically unavailable as-drawn. Legacy remains existing driver. */
 if(mode!=FILL_MODE_AUX||action!=FILL_ACTION_RETRACT)return FILL_MOTION_UNSUPPORTED;
 if(!fill_source_fresh(source,now))return FILL_MOTION_STALE;
 if(!fill_source_after(source,m->last_source,now)){
  if(source==m->last_source)return action==m->last_action?FILL_MOTION_DUPLICATE:FILL_MOTION_STALE;
  return FILL_MOTION_STALE;
 }
 if(m->active)return FILL_MOTION_BUSY;
 m->last_source=source;m->last_action=action;m->active_action=action;m->started_ms=tick;m->active=true;m->completed=false;return FILL_MOTION_STARTED;
}
bool fill_motion_service(FillMotion*m,uint32_t tick){
 if(!m->active||(uint32_t)(tick-m->started_ms)<FILL_DURATION_MS)return false;
 m->active=false;m->active_action=FILL_ACTION_NONE;m->completed=true;return true;
}
void fill_motion_stop(FillMotion*m){m->active=false;m->active_action=FILL_ACTION_NONE;m->completed=false;}
uint32_t fill_motion_remaining(const FillMotion*m,uint32_t tick){if(!m->active)return 0U;uint32_t elapsed=tick-m->started_ms;return elapsed>=FILL_DURATION_MS?0U:FILL_DURATION_MS-elapsed;}
