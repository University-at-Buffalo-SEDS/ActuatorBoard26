#include "stepper_driver.h"

static inline void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks);
}


//Ima be real, no clue on this one. The stepper stuff is a wee bit over my head. 
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

void stepperInit(stepper_t *hw)
{
    dwt_init();

    HAL_GPIO_WritePin(hw->step_port,  hw->step_pin,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(hw->dir_port,   hw->dir_pin,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(hw->reset_port, hw->reset_pin, GPIO_PIN_RESET);
    delay_us(10);
    HAL_GPIO_WritePin(hw->reset_port, hw->reset_pin, GPIO_PIN_SET);

    HAL_GPIO_WritePin(hw->sleep_port, hw->sleep_pin, GPIO_PIN_SET);
    HAL_Delay(SLEEP_WAKE_MS);

    hw->is_sleeping = false;
    hw->direction   = STEP_CW;
}

void stepperSetDir(stepper_t *hw, uint8_t dir)
{
    hw->direction = dir;
    HAL_GPIO_WritePin(hw->dir_port, hw->dir_pin, dir == STEP_CW ? GPIO_PIN_RESET : GPIO_PIN_SET);
    delay_us(DIR_SETUP_US);
}

int stepperSingleStep(stepper_t *hw)
{
    if (hw->is_sleeping) {
        return STEP_SLEEPING;
    }

    HAL_GPIO_WritePin(hw->step_port, hw->step_pin, GPIO_PIN_SET);
    delay_us(STEP_PULSE_US);
    HAL_GPIO_WritePin(hw->step_port, hw->step_pin, GPIO_PIN_RESET);
    delay_us(STEP_PULSE_US);

    return STEP_OK;
}

int stepperMoveSteps(stepper_t *hw, uint32_t steps, uint32_t period_us)
{
    if (hw->is_sleeping) {
        return STEP_SLEEPING;
    }

    if (period_us < (STEP_PULSE_US * 2)) {
        period_us = STEP_PULSE_US * 2;
    }

    uint32_t low_us = period_us - STEP_PULSE_US;

    for (uint32_t i = 0; i < steps; i++) {
        HAL_GPIO_WritePin(hw->step_port, hw->step_pin, GPIO_PIN_SET);
        delay_us(STEP_PULSE_US);
        HAL_GPIO_WritePin(hw->step_port, hw->step_pin, GPIO_PIN_RESET);
        delay_us(low_us);
    }

    return STEP_OK;
}

void stepperSleep(stepper_t *hw)
{
    HAL_GPIO_WritePin(hw->sleep_port, hw->sleep_pin, GPIO_PIN_RESET);
    hw->is_sleeping = true;
}

int stepperWake(stepper_t *hw)
{
    HAL_GPIO_WritePin(hw->sleep_port, hw->sleep_pin, GPIO_PIN_SET);
    HAL_Delay(SLEEP_WAKE_MS);
    hw->is_sleeping = false;
    return STEP_OK;
}

void stepperReset(stepper_t *hw)
{
    HAL_GPIO_WritePin(hw->reset_port, hw->reset_pin, GPIO_PIN_RESET);
    delay_us(10);
    HAL_GPIO_WritePin(hw->reset_port, hw->reset_pin, GPIO_PIN_SET);
    stepperSetDir(hw, hw->direction);
}