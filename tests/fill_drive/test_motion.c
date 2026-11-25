#include "fill_motion.h"
#include "fill_bridge.h"
#include "board_stamp.h"
#include <assert.h>
#include <stdio.h>
static unsigned writes;static uint8_t mask;
static void write_mask(uint8_t v){assert(v==0||v==3||v==12);mask=v;writes++;}
int main(void){
 FillMotion m={0};assert(fill_motion_start(&m,2,1,10000,true,100)==0&&m.active);assert(fill_motion_remaining(&m,101)==24999);assert(!fill_motion_service(&m,25099));
 assert(fill_motion_start(&m,2,1,10000,true,200)==1&&m.started_ms==100);assert(fill_motion_start(&m,2,1,10001,true,200)==2&&m.started_ms==100);assert(!fill_motion_service(&m,25099));assert(fill_motion_service(&m,25100)&&m.completed&&!m.active);assert(!fill_motion_service(&m,25101));
 assert(fill_motion_start(&m,2,1,10000,true,25102)==1&&!m.active);assert(fill_motion_start(&m,2,1,9999,true,25102)==3);assert(fill_motion_start(&m,2,2,10002,true,50000)==4);assert(fill_motion_start(&m,1,1,10002,true,50000)==4);assert(fill_motion_start(&m,0,1,10002,true,50000)==4);
 assert(fill_motion_start(&m,2,1,10002,true,UINT32_MAX-10)==0);assert(!fill_motion_service(&m,20));assert(fill_motion_remaining(&m,20)==24969);assert(fill_motion_service(&m,24989));
 assert(fill_motion_start(&m,2,1,10003,true,7)==0);fill_motion_stop(&m);assert(!m.active&&!m.completed&&fill_motion_remaining(&m,8)==0);assert(fill_motion_start(&m,2,1,10003,true,9)==1&&!m.active);
 /* Invalid stamps and a stamp SEDSnet judged outside the 5 s/1 s window. */
 for(unsigned i=0;i<3;i++){FillMotion n={0};uint64_t source=i==0?0:i==1?UINT64_MAX:10000;assert(fill_motion_start(&n,2,1,source,i==2?false:true,1)==3&&!n.active);}
 /* A 4 h network time step back: the next QD is stamped in a newer epoch and
  * orders after the last one; the old stamp and a duplicate stay refused. */
 {FillMotion n={0};const uint64_t before=board_stamp_pack(0,UINT64_C(1794183589988)),after=board_stamp_pack(1,UINT64_C(1794183589988)-UINT64_C(14400000));
  assert(fill_motion_start(&n,2,1,before,true,1)==0);assert(fill_motion_service(&n,1+25000));
  assert(fill_motion_start(&n,2,1,after,true,50000)==0&&n.active&&n.last_source==after);
  assert(fill_motion_start(&n,2,1,before,true,50001)==3);assert(fill_motion_start(&n,2,1,after,true,50002)==1);
  /* A forward 3.8 h step orders the same way. */
  fill_motion_stop(&n);const uint64_t ahead=board_stamp_pack(2,UINT64_C(1794183589988)+UINT64_C(13680000));
  assert(fill_motion_start(&n,2,1,ahead,true,60000)==0&&n.last_source==ahead);}
 FillBridge b;fill_bridge_init(&b,false,2,write_mask);assert(!mask&&!fill_bridge_request(&b,1,0,false));fill_bridge_service(&b,3,false);assert(!mask&&!b.active);fill_bridge_init(&b,true,2,write_mask);assert(fill_bridge_request(&b,1,10,false)&&!mask);fill_bridge_service(&b,11,false);assert(!mask);fill_bridge_service(&b,12,false);assert(mask==3&&b.active);assert(fill_bridge_request(&b,1,13,false)&&b.started==12);assert(fill_bridge_request(&b,2,14,false)&&!mask&&!b.active);fill_bridge_service(&b,15,false);assert(!mask);fill_bridge_service(&b,16,false);assert(mask==12);fill_bridge_service(&b,17,true);assert(!mask&&b.fault&&!b.active);assert(!fill_bridge_request(&b,1,20,false));
 fill_bridge_init(&b,true,2,write_mask);assert(fill_bridge_request(&b,1,UINT32_MAX,false));fill_bridge_service(&b,0,false);assert(!mask);fill_bridge_service(&b,1,false);assert(mask==3);fill_bridge_service(&b,25000,false);assert(mask==3);fill_bridge_service(&b,25001,false);assert(!mask&&!b.active);fill_bridge_init(&b,true,0,write_mask);assert(!fill_bridge_request(&b,1,0,false));
 printf("Nonblocking 25 s motion, retries, wrap, Stop and logical bridge blanking/fault proof PASS (%u logical callback writes); no physical activation\n",writes);return 0;
}
