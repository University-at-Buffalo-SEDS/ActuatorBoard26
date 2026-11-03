#include "fill_bridge.h"
/* Logical mask bits: P_FWD,N_FWD,P_BCKWD,N_BCKWD. No pin mappings. */
static void off(FillBridge*b){if(b->write)b->write(0U);b->active=false;b->direction=0U;}
void fill_bridge_init(FillBridge*b,bool available,uint32_t blank,void(*write)(uint8_t)){*b=(FillBridge){0};b->available=available&&blank>0U;b->blank_ms=blank;b->write=write;off(b);}
void fill_bridge_stop(FillBridge*b){off(b);b->blanking=false;b->pending=0U;}
bool fill_bridge_request(FillBridge*b,uint8_t dir,uint32_t now,bool fault){
 if(fault){b->fault=true;fill_bridge_stop(b);return false;}
 if(!b->available||b->fault||dir<1U||dir>2U)return false;
 if((b->active&&b->direction==dir)||(b->blanking&&b->pending==dir))return true; /* No deadline extension. */
 off(b);b->blanking=true;b->pending=dir;b->blank_started=now;return true;
}
void fill_bridge_service(FillBridge*b,uint32_t now,bool fault){
 if(fault){b->fault=true;fill_bridge_stop(b);return;}
 if(!b->available||b->fault){fill_bridge_stop(b);return;}
 if(b->blanking&&(uint32_t)(now-b->blank_started)>=b->blank_ms){b->direction=b->pending;b->pending=0U;b->blanking=false;b->started=now;b->active=true;if(b->write)b->write(b->direction==1U?3U:12U);}
 if(b->active&&(uint32_t)(now-b->started)>=45000U)fill_bridge_stop(b);
}
