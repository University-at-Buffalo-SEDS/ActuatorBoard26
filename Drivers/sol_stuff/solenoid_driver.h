#pragma once

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>


/* en_port/en_pin is the drive output (N2_Valve_EN PB3, N20_Valve_EN PB4:
 * Schematic.pdf p.2 STM32.SchDoc -> p.3 Solenoid_Drive.SchDoc, 2N7002 gate
 * with a 10k pull-down -> LTV-827 opto -> IRFR9024 high side). The
 * N2_Sig/N20_Sig nets (PB13/PB14) are ribbon-cable inputs (J6, p.8
 * Connectors.SchDoc), not drive lines, so the driver never touches them. */
typedef struct {
    GPIO_TypeDef *en_port;
    uint16_t      en_pin;
    GPIO_TypeDef *fault_port;
    uint16_t      fault_pin;
    uint8_t       adc_channel;
} solenoid_t;

void solenoidInit(solenoid_t *hw);
int solenoidOn(solenoid_t *hw);
void solenoidOff(solenoid_t *hw);
bool solenoidFault(solenoid_t *hw);