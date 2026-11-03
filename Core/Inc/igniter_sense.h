#ifndef IGNITER_SENSE_H
#define IGNITER_SENSE_H
#include <stdint.h>
#include "telemetry.h"
/* Read-only; capture/fault are called only by the existing ADC owner ISR.
 * poll is called only while the telemetry owner holds its router lock. */
void igniter_sense_capture(uint16_t counts, uint32_t sampled_ms);
void igniter_sense_fault(void);
#ifdef TELEMETRY_ENABLED
void igniter_sense_poll(SedsRouter *router);
#endif
#endif
