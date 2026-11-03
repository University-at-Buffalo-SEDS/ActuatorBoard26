#ifndef BOARD_TEMPERATURE_H
#define BOARD_TEMPERATURE_H
#include <stdint.h>
#include <stdbool.h>
/* Called only from the ADC owner on AB/DAQ; health poll on dedicated ADC boards.
 * Best effort, at most one pair / 5s. Never drives control or watchdog health. */
void board_temperature_poll(void);
uint32_t board_temperature_health_flags(void);
/* Same injected VREF sample; this does not start/reconfigure the ADC. */
bool board_temperature_reference_mv(uint32_t *millivolts, uint32_t *age_ms);
#endif
