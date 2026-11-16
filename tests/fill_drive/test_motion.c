#include "fill_motion.h"
#include "fill_bridge.h"
#include <assert.h>
#include <stdio.h>
static unsigned writes;static uint8_t mask;
static void write_mask(uint8_t v){assert(v==0||v==3||v==12);mask=v;writes++;}
int main(void){
 FillMotion m={0};assert(fill_motion_start(&m,2,1,10000,10000,100)==0&&m.active);assert(fill_motion_remaining(&m,101)==44999);assert(!fill_motion_service(&m,45099));
 assert(fill_motion_start(&m,2,1,10000,10001,200)==1&&m.started_ms==100);assert(fill_motion_start(&m,2,1,10001,10001,200)==2&&m.started_ms==100);assert(!fill_motion_service(&m,45099));assert(fill_motion_service(&m,45100)&&m.completed&&!m.active);assert(!fill_motion_service(&m,45101));
 assert(fill_motion_start(&m,2,1,10000,10000,45102)==1&&!m.active);assert(fill_motion_start(&m,2,1,9999,10000,45102)==3);assert(fill_motion_start(&m,2,2,10002,10002,50000)==4);assert(fill_motion_start(&m,1,1,10002,10002,50000)==4);assert(fill_motion_start(&m,0,1,10002,10002,50000)==4);
 assert(fill_motion_start(&m,2,1,10002,10002,UINT32_MAX-10)==0);assert(!fill_motion_service(&m,20));assert(fill_motion_remaining(&m,20)==44969);assert(fill_motion_service(&m,44989));
 assert(fill_motion_start(&m,2,1,10003,10003,7)==0);fill_motion_stop(&m);assert(!m.active&&!m.completed&&fill_motion_remaining(&m,8)==0);assert(fill_motion_start(&m,2,1,10003,10003,9)==1&&!m.active);
 for(unsigned i=0;i<5;i++){FillMotion n={0};uint64_t source=i==0?0:i==1?UINT64_MAX:i==2?1:i==3?12000:10000;uint64_t now=i==4?0:10000;assert(fill_motion_start(&n,2,1,source,now,1)==3&&!n.active);}
 /* Station clock stepped back 4 h after an accepted retract: a fresh retract on
  * the new clock starts; an old stamp (stale or ahead) never does. */
 {const uint64_t H4=UINT64_C(14400000),T=UINT64_C(1794183600000);FillMotion k={0};
  assert(fill_motion_start(&k,2,1,T,T,1)==0);fill_motion_stop(&k);
  assert(fill_motion_start(&k,2,1,T-H4+10,T-H4+10,2)==0&&k.active&&k.last_source==T-H4+10);fill_motion_stop(&k);
  assert(fill_motion_start(&k,2,1,T,T-H4+20,3)==3);
  assert(fill_motion_start(&k,2,1,T-H4+10,T-H4+20,3)==1);
  assert(fill_motion_start(&k,2,1,T-H4+5,T-H4+20,3)==3);
  /* Within a minute the mark still orders: no bypass for a small step. */
  FillMotion j={0};assert(fill_motion_start(&j,2,1,T,T,1)==0);fill_motion_stop(&j);
  assert(fill_motion_start(&j,2,1,T-30000,T-30000,2)==3);
  /* Forward 4 h step: newer stamps order after the mark already. */
  assert(fill_motion_start(&j,2,1,T+H4,T+H4,4)==0);
  assert(fill_source_after(T-H4+10,T,T-H4+20)&&!fill_source_after(T-H4+10,T,T-H4+9000)&&!fill_source_after(T-50000,T,T-50000));}
 FillBridge b;fill_bridge_init(&b,false,2,write_mask);assert(!mask&&!fill_bridge_request(&b,1,0,false));fill_bridge_service(&b,3,false);assert(!mask&&!b.active);fill_bridge_init(&b,true,2,write_mask);assert(fill_bridge_request(&b,1,10,false)&&!mask);fill_bridge_service(&b,11,false);assert(!mask);fill_bridge_service(&b,12,false);assert(mask==3&&b.active);assert(fill_bridge_request(&b,1,13,false)&&b.started==12);assert(fill_bridge_request(&b,2,14,false)&&!mask&&!b.active);fill_bridge_service(&b,15,false);assert(!mask);fill_bridge_service(&b,16,false);assert(mask==12);fill_bridge_service(&b,17,true);assert(!mask&&b.fault&&!b.active);assert(!fill_bridge_request(&b,1,20,false));
 fill_bridge_init(&b,true,2,write_mask);assert(fill_bridge_request(&b,1,UINT32_MAX,false));fill_bridge_service(&b,0,false);assert(!mask);fill_bridge_service(&b,1,false);assert(mask==3);fill_bridge_service(&b,45000,false);assert(mask==3);fill_bridge_service(&b,45001,false);assert(!mask&&!b.active);fill_bridge_init(&b,true,0,write_mask);assert(!fill_bridge_request(&b,1,0,false));
 printf("Nonblocking45s motion, retries, wrap, Stop and logical bridge blanking/fault proof PASS (%u logical callback writes); no physical activation\n",writes);return 0;
}
