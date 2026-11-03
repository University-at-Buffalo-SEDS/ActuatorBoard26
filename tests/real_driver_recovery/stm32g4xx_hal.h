#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct {uint32_t ODR;} GPIO_TypeDef;
typedef struct {uint32_t CR1,RCR,ARR,CNT,SR,EGR,DIER,BDTR,CCER;} TIM_TypeDef;
typedef struct {TIM_TypeDef *Instance;} TIM_HandleTypeDef;
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {uint32_t OCMode,Pulse,OCPolarity,OCNPolarity,OCFastMode,OCIdleState,OCNIdleState;} TIM_OC_InitTypeDef;
typedef struct {uint32_t CYCCNT,CTRL;} DWT_Type;
typedef struct {uint32_t DEMCR;} CoreDebug_Type;
extern TIM_TypeDef fake_tim1;
extern CoreDebug_Type fake_core_debug;
extern uint32_t SystemCoreClock;
DWT_Type *fake_dwt(void);
#define DWT (fake_dwt())
#define CoreDebug (&fake_core_debug)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define TIM1 (&fake_tim1)
#define TIM_CHANNEL_3 3U
#define TIM_CR1_CEN 1U
#define TIM_CR1_OPM 8U
#define TIM_IT_UPDATE 1U
#define TIM_FLAG_UPDATE 1U
#define TIM_EGR_UG 1U
#define TIM_CCER_CC3NE (1U<<10)
#define TIM_BDTR_MOE 0x8000U
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_HIGH 3U
#define GPIO_AF6_TIM1 6U
#define TIM_OCMODE_PWM1 6U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCNPOLARITY_HIGH 0U
#define TIM_OCFAST_DISABLE 0U
#define TIM_OCIDLESTATE_RESET 0U
#define TIM_OCNIDLESTATE_RESET 0U
typedef int HAL_StatusTypeDef;
#define TIM_DIER_UIE TIM_IT_UPDATE
#define HAL_OK 0
#define HAL_ERROR 1
void fake_timer_disable(TIM_HandleTypeDef*);
#define __HAL_TIM_DISABLE(h) fake_timer_disable(h)
#define __HAL_TIM_DISABLE_IT(h,v) ((h)->Instance->DIER &= ~(v))
#define __HAL_TIM_ENABLE_IT(h,v) ((h)->Instance->DIER |= (v))
#define __HAL_TIM_SET_AUTORELOAD(h,v) ((h)->Instance->ARR=(v))
#define __HAL_TIM_SET_COUNTER(h,v) ((h)->Instance->CNT=(v))
#define __HAL_TIM_CLEAR_FLAG(h,v) ((h)->Instance->SR &= ~(v))
void fake_clear_bits(uint32_t*,uint32_t);
#define CLEAR_BIT(reg,v) fake_clear_bits(&(reg),v)
void HAL_GPIO_WritePin(GPIO_TypeDef*,uint16_t,unsigned);
void HAL_GPIO_Init(GPIO_TypeDef*,GPIO_InitTypeDef*);
void HAL_Delay(uint32_t);
int HAL_TIMEx_PWMN_Stop(TIM_HandleTypeDef*,uint32_t);
int HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef*,uint32_t);
int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef*,TIM_OC_InitTypeDef*,uint32_t);

#define __DMB() __asm__ volatile("" ::: "memory")
