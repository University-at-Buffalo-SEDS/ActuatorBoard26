/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void cdc_printf_init(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define H_BRIDGE_FAULT_Pin GPIO_PIN_0
#define H_BRIDGE_FAULT_GPIO_Port GPIOC
#define N20_FAULT_Pin GPIO_PIN_1
#define N20_FAULT_GPIO_Port GPIOC
#define N2_FAULT_Pin GPIO_PIN_2
#define N2_FAULT_GPIO_Port GPIOC
#define IGNITER_FAULT_Pin GPIO_PIN_3
#define IGNITER_FAULT_GPIO_Port GPIOC
#define N20_ADC_Pin GPIO_PIN_0
#define N20_ADC_GPIO_Port GPIOA
#define N2_ADC_Pin GPIO_PIN_1
#define N2_ADC_GPIO_Port GPIOA
#define IGNITER_ADC_Pin GPIO_PIN_2
#define IGNITER_ADC_GPIO_Port GPIOA
#define STEPPER_RESET_Pin GPIO_PIN_3
#define STEPPER_RESET_GPIO_Port GPIOA
#define STEPPER_SLEEP_Pin GPIO_PIN_4
#define STEPPER_SLEEP_GPIO_Port GPIOA
#define H_BRIDGE_ADC_Pin GPIO_PIN_6
#define H_BRIDGE_ADC_GPIO_Port GPIOA
#define H_BRIDGE_BCKWD_BTN_EN_Pin GPIO_PIN_7
#define H_BRIDGE_BCKWD_BTN_EN_GPIO_Port GPIOA
#define H_BRIDGE_FWD_BTN_EN_Pin GPIO_PIN_4
#define H_BRIDGE_FWD_BTN_EN_GPIO_Port GPIOC
#define STEPPER_BCKWD_BTN_EN_Pin GPIO_PIN_5
#define STEPPER_BCKWD_BTN_EN_GPIO_Port GPIOC
#define STEPPER_FWD_BTN_EN_Pin GPIO_PIN_0
#define STEPPER_FWD_BTN_EN_GPIO_Port GPIOB
#define STEPPER_CTRL_Pin GPIO_PIN_1
#define STEPPER_CTRL_GPIO_Port GPIOB
#define STEPPER_DIR_Pin GPIO_PIN_2
#define STEPPER_DIR_GPIO_Port GPIOB
#define GENERAL_FAULT_SIG_Pin GPIO_PIN_11
#define GENERAL_FAULT_SIG_GPIO_Port GPIOB
#define IGNITER_SIG_Pin GPIO_PIN_12
#define IGNITER_SIG_GPIO_Port GPIOB
#define N2_SIG_Pin GPIO_PIN_13
#define N2_SIG_GPIO_Port GPIOB
#define N20_SIG_Pin GPIO_PIN_14
#define N20_SIG_GPIO_Port GPIOB
#define STEPPER_BCKWD_SIG_Pin GPIO_PIN_15
#define STEPPER_BCKWD_SIG_GPIO_Port GPIOB
#define STEPPER_FWD_SIG_Pin GPIO_PIN_6
#define STEPPER_FWD_SIG_GPIO_Port GPIOC
#define H_BRIDGE_BCKWD_SIG_Pin GPIO_PIN_7
#define H_BRIDGE_BCKWD_SIG_GPIO_Port GPIOC
#define H_BRIDGE_FWD_SIG_Pin GPIO_PIN_8
#define H_BRIDGE_FWD_SIG_GPIO_Port GPIOC
#define N_BCKWD_EN_Pin GPIO_PIN_9
#define N_BCKWD_EN_GPIO_Port GPIOC
#define N_FWD_EN_Pin GPIO_PIN_8
#define N_FWD_EN_GPIO_Port GPIOA
#define P_BCKWD_EN_Pin GPIO_PIN_9
#define P_BCKWD_EN_GPIO_Port GPIOA
#define P_FWD_EN_Pin GPIO_PIN_10
#define P_FWD_EN_GPIO_Port GPIOA
#define GREEN_LED_Pin GPIO_PIN_15
#define GREEN_LED_GPIO_Port GPIOA
#define BACKUP_VALVE_EN_Pin GPIO_PIN_2
#define BACKUP_VALVE_EN_GPIO_Port GPIOD
#define NITROUS_PIN_Pin GPIO_PIN_3
#define NITROUS_PIN_GPIO_Port GPIOB
#define NITROGEN_PIN_Pin GPIO_PIN_4
#define NITROGEN_PIN_GPIO_Port GPIOB
#define IGNITER_PIN_Pin GPIO_PIN_7
#define IGNITER_PIN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
