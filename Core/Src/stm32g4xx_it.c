/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32g4xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32g4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
typedef struct
{
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t xpsr;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t dfsr;
  uint32_t afsr;
  uint32_t bfar;
  uint32_t mmfar;
  uint32_t exc_return;
} hardfault_info_t;

volatile hardfault_info_t g_hardfault_info = {0U};

volatile uint32_t g_hardfault_r0 = 0U;
volatile uint32_t g_hardfault_r1 = 0U;
volatile uint32_t g_hardfault_r2 = 0U;
volatile uint32_t g_hardfault_r3 = 0U;
volatile uint32_t g_hardfault_r12 = 0U;
volatile uint32_t g_hardfault_lr = 0U;
volatile uint32_t g_hardfault_pc = 0U;
volatile uint32_t g_hardfault_xpsr = 0U;
volatile uint32_t g_hardfault_cfsr = 0U;
volatile uint32_t g_hardfault_hfsr = 0U;
volatile uint32_t g_hardfault_dfsr = 0U;
volatile uint32_t g_hardfault_afsr = 0U;
volatile uint32_t g_hardfault_bfar = 0U;
volatile uint32_t g_hardfault_mmfar = 0U;
volatile uint32_t g_hardfault_exc_return = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void hardfault_capture(uint32_t *stacked_regs, uint32_t exc_return);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void hardfault_capture(uint32_t *stacked_regs, uint32_t exc_return)
{
  g_hardfault_info.r0 = stacked_regs[0];
  g_hardfault_info.r1 = stacked_regs[1];
  g_hardfault_info.r2 = stacked_regs[2];
  g_hardfault_info.r3 = stacked_regs[3];
  g_hardfault_info.r12 = stacked_regs[4];
  g_hardfault_info.lr = stacked_regs[5];
  g_hardfault_info.pc = stacked_regs[6];
  g_hardfault_info.xpsr = stacked_regs[7];
  g_hardfault_info.cfsr = SCB->CFSR;
  g_hardfault_info.hfsr = SCB->HFSR;
  g_hardfault_info.dfsr = SCB->DFSR;
  g_hardfault_info.afsr = SCB->AFSR;
  g_hardfault_info.bfar = SCB->BFAR;
  g_hardfault_info.mmfar = SCB->MMFAR;
  g_hardfault_info.exc_return = exc_return;

  g_hardfault_r0 = g_hardfault_info.r0;
  g_hardfault_r1 = g_hardfault_info.r1;
  g_hardfault_r2 = g_hardfault_info.r2;
  g_hardfault_r3 = g_hardfault_info.r3;
  g_hardfault_r12 = g_hardfault_info.r12;
  g_hardfault_lr = g_hardfault_info.lr;
  g_hardfault_pc = g_hardfault_info.pc;
  g_hardfault_xpsr = g_hardfault_info.xpsr;
  g_hardfault_cfsr = g_hardfault_info.cfsr;
  g_hardfault_hfsr = g_hardfault_info.hfsr;
  g_hardfault_dfsr = g_hardfault_info.dfsr;
  g_hardfault_afsr = g_hardfault_info.afsr;
  g_hardfault_bfar = g_hardfault_info.bfar;
  g_hardfault_mmfar = g_hardfault_info.mmfar;
  g_hardfault_exc_return = g_hardfault_info.exc_return;

  __BKPT(0);

  while (1)
  {
  }
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern FDCAN_HandleTypeDef hfdcan2;
extern PCD_HandleTypeDef hpcd_USB_FS;
extern TIM_HandleTypeDef htim6;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  __asm volatile(
      "tst lr, #4                                \n"
      "ite eq                                    \n"
      "mrseq r0, msp                             \n"
      "mrsne r0, psp                             \n"
      "mov r1, lr                                \n"
      "b hardfault_capture                       \n");
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32G4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32g4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles USB low priority interrupt remap.
  */
void USB_LP_IRQHandler(void)
{
  /* USER CODE BEGIN USB_LP_IRQn 0 */

  /* USER CODE END USB_LP_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
  /* USER CODE BEGIN USB_LP_IRQn 1 */

  /* USER CODE END USB_LP_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt, DAC1 and DAC3 channel underrun error interrupts.
  */
void TIM6_DAC_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_DAC_IRQn 0 */

  /* USER CODE END TIM6_DAC_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_DAC_IRQn 1 */

  /* USER CODE END TIM6_DAC_IRQn 1 */
}

/**
  * @brief This function handles FDCAN2 interrupt 0.
  */
void FDCAN2_IT0_IRQHandler(void)
{
  /* USER CODE BEGIN FDCAN2_IT0_IRQn 0 */

  /* USER CODE END FDCAN2_IT0_IRQn 0 */
  HAL_FDCAN_IRQHandler(&hfdcan2);
  /* USER CODE BEGIN FDCAN2_IT0_IRQn 1 */

  /* USER CODE END FDCAN2_IT0_IRQn 1 */
}

/**
  * @brief This function handles FDCAN2 interrupt 1.
  */
void FDCAN2_IT1_IRQHandler(void)
{
  /* USER CODE BEGIN FDCAN2_IT1_IRQn 0 */

  /* USER CODE END FDCAN2_IT1_IRQn 0 */
  HAL_FDCAN_IRQHandler(&hfdcan2);
  /* USER CODE BEGIN FDCAN2_IT1_IRQn 1 */

  /* USER CODE END FDCAN2_IT1_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
