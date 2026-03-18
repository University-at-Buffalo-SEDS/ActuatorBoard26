#pragma once

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    GPIO_TypeDef *en_port;
    uint16_t      en_pin;
    GPIO_TypeDef *fault_port;
    uint16_t      fault_pin;
    uint8_t       adc_channel;
} igniter_t;

void igniterInit(igniter_t *hw);
int  igniterOn(igniter_t *hw);
void igniterOff(igniter_t *hw);
bool igniterFault(igniter_t *hw);