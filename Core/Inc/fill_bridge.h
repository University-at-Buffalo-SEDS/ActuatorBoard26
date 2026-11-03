#ifndef FILL_BRIDGE_H
#define FILL_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
/* Pure logical driver, not wired to hardware. Direction1/2 means electrical
 * polarity, not mechanical retract/extend. Every write callback is bounded. */
typedef struct {bool available,fault,active,blanking;uint8_t direction,pending;uint32_t blank_started,started,blank_ms;void(*write)(uint8_t);} FillBridge;
void fill_bridge_init(FillBridge*b,bool available,uint32_t verified_blank_ms,void(*write)(uint8_t));
bool fill_bridge_request(FillBridge*b,uint8_t electrical_direction,uint32_t now,bool fault);
void fill_bridge_service(FillBridge*b,uint32_t now,bool fault);
void fill_bridge_stop(FillBridge*b);
#endif
