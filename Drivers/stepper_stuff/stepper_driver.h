#pragma once

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define STEP_OK       0
#define STEP_FAULT   -1
#define STEP_SLEEPING -2
#define STEP_CW       0
#define STEP_CCW      1
#define STEP_PULSE_US  3
#define DIR_SETUP_US   1
#define SLEEP_WAKE_MS  2

typedef struct {
    GPIO_TypeDef *step_port;
    uint16_t      step_pin;
    GPIO_TypeDef *dir_port;
    uint16_t      dir_pin;
    GPIO_TypeDef *reset_port;
    uint16_t      reset_pin;
    GPIO_TypeDef *sleep_port;
    uint16_t      sleep_pin;
    bool          is_sleeping;
    uint8_t       direction;
} stepper_t;

void stepperInit(stepper_t *hw);
void stepperSetDir(stepper_t *hw, uint8_t dir);
int  stepperSingleStep(stepper_t *hw);
int  stepperMoveSteps(stepper_t *hw, uint32_t steps, uint32_t period_us);
void stepperSleep(stepper_t *hw);
int  stepperWake(stepper_t *hw);
void stepperReset(stepper_t *hw);