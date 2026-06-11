#include "stepper_driver.h"

extern TIM_HandleTypeDef htim1;

static volatile stepper_t *timer_stepper = NULL;
static volatile bool timer_move_active = false;
static volatile uint32_t timer_steps_remaining = 0U;
static volatile uint32_t timer_period_us = 0U;

#define STEPPER_PWM_CHANNEL TIM_CHANNEL_3

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

static void configureStepPinAsGpio(stepper_t *hw)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(hw->step_port, hw->step_pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = hw->step_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(hw->step_port, &GPIO_InitStruct);
}

static void configureStepPinAsTimer(stepper_t *hw)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(hw->step_port, hw->step_pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = hw->step_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(hw->step_port, &GPIO_InitStruct);
}

static int startTimerMove(stepper_t *hw)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    (void)HAL_TIMEx_PWMN_Stop(&htim1, STEPPER_PWM_CHANNEL);
    __HAL_TIM_DISABLE(&htim1);
    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = STEP_PULSE_US;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, STEPPER_PWM_CHANNEL) != HAL_OK) {
        configureStepPinAsGpio(hw);
        return STEP_FAULT;
    }

    htim1.Instance->CR1 &= ~TIM_CR1_OPM;
    htim1.Instance->RCR = 0U;
    __HAL_TIM_SET_AUTORELOAD(&htim1, timer_period_us - 1U);
    __HAL_TIM_SET_COUNTER(&htim1, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    htim1.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);

    configureStepPinAsTimer(hw);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);

    if (HAL_TIMEx_PWMN_Start(&htim1, STEPPER_PWM_CHANNEL) != HAL_OK) {
        __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
        configureStepPinAsGpio(hw);
        return STEP_FAULT;
    }

    return STEP_OK;
}

void stepperInit(stepper_t *hw)
{
    dwt_init();

    configureStepPinAsGpio(hw);
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

int stepperStartMoveTimer(stepper_t *hw, uint32_t steps, uint32_t period_us)
{
    if (hw->is_sleeping) {
        return STEP_SLEEPING;
    }

    if ((steps == 0U) || (period_us <= STEP_PULSE_US)) {
        return STEP_FAULT;
    }

    if (timer_move_active) {
        return STEP_BUSY;
    }

    timer_stepper = hw;
    timer_steps_remaining = steps;
    timer_period_us = period_us;
    timer_move_active = true;

    if (startTimerMove(hw) != STEP_OK) {
        timer_steps_remaining = 0U;
        timer_period_us = 0U;
        timer_move_active = false;
        timer_stepper = NULL;
        return STEP_FAULT;
    }

    return STEP_OK;
}

bool stepperIsMoveActive(const stepper_t *hw)
{
    if (!timer_move_active || (timer_stepper != hw)) {
        return false;
    }

    return (htim1.Instance->CR1 & TIM_CR1_CEN) != 0U;
}

void stepperStopMoveTimer(stepper_t *hw)
{
    if (timer_stepper == hw) {
        __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
        (void)HAL_TIMEx_PWMN_Stop(&htim1, STEPPER_PWM_CHANNEL);
        __HAL_TIM_DISABLE(&htim1);
        CLEAR_BIT(htim1.Instance->BDTR, TIM_BDTR_MOE);
        configureStepPinAsGpio(hw);
        timer_steps_remaining = 0U;
        timer_period_us = 0U;
        timer_move_active = false;
        timer_stepper = NULL;
    }
}

void stepperTimerPeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if ((htim->Instance != TIM1) || !timer_move_active) {
        return;
    }

    if (timer_steps_remaining > 1U) {
        timer_steps_remaining--;
        return;
    }

    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
    (void)HAL_TIMEx_PWMN_Stop(&htim1, STEPPER_PWM_CHANNEL);
    __HAL_TIM_DISABLE(&htim1);
    CLEAR_BIT(htim1.Instance->BDTR, TIM_BDTR_MOE);
    timer_steps_remaining = 0U;
    timer_period_us = 0U;
    timer_move_active = false;
    timer_stepper = NULL;
}

void stepperSleep(stepper_t *hw)
{
    stepperStopMoveTimer(hw);
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
