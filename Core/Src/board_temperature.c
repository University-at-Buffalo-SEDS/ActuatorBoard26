#include "board_temperature.h"
#include "board_temperature_math.h"
#include "igniter_sense_math.h"
#include "main.h"
#include <stdbool.h>
#include "stm32g4xx_ll_adc.h"
static uint32_t last_attempt;
static uint32_t sample_ms;
static int16_t sample_centi;
static bool valid;
static bool attempted;
static uint32_t reference_mv, reference_sample_ms;
static bool reference_ok;
static bool due(uint32_t now)
{
  if (attempted && (uint32_t)(now - last_attempt) < 5000U) return false;
  attempted = true; last_attempt = now; return true;
}
static void publish(uint32_t raw, uint32_t ref, uint32_t now)
{
  int16_t c;
  bool ok = board_temperature_convert(raw, ref, *VREFINT_CAL_ADDR,
      *TEMPSENSOR_CAL1_ADDR, *TEMPSENSOR_CAL2_ADDR,
      TEMPSENSOR_CAL1_TEMP, TEMPSENSOR_CAL2_TEMP, &c);
  uint32_t mv = 0U;
  const bool ref_ok = igniter_sense_vdda_mv(ref, *VREFINT_CAL_ADDR, VREFINT_CAL_VREF, &mv);
  uint32_t irq = __get_PRIMASK(); __disable_irq();
  if (ref_ok) { reference_mv = mv; reference_sample_ms = now; }
  reference_ok = ref_ok;
  if (ok) { sample_centi = c; sample_ms = now; }
  valid = ok;
  __set_PRIMASK(irq);
}
uint32_t board_temperature_health_flags(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t irq = __get_PRIMASK(); __disable_irq();
  uint32_t flags = valid ? board_temperature_pack(sample_centi, now - sample_ms) : 0U;
  __set_PRIMASK(irq);
  return flags;
}

bool board_temperature_reference_mv(uint32_t *millivolts, uint32_t *age_ms)
{
  if (!millivolts || !age_ms) return false;
  const uint32_t now = HAL_GetTick();
  const uint32_t irq = __get_PRIMASK(); __disable_irq();
  *millivolts = reference_mv;
  *age_ms = (uint32_t)(now - reference_sample_ms);
  const bool ok = reference_ok && *age_ms <= IGNITER_SENSE_REFERENCE_MAX_AGE_MS;
  __set_PRIMASK(irq);
  return ok;
}

static bool wait_bits(volatile uint32_t *reg, uint32_t mask, bool set)
{
  uint32_t budget = SystemCoreClock / 10000U + 1U;
  while (((*reg & mask) != 0U) != set) if (--budget == 0U) return false;
  return true;
}
void board_temperature_poll(void)
{
  uint32_t now = HAL_GetTick();
  /* Only safety_task calls this, before starting its finite regular DMA scan.
   * Never preempt safety conversions, stop DMA, change regular SQR/CFGR, or
   * recalibrate an ADC owned by safety. An unexpectedly busy ADC is skipped. */
  if ((ADC1->CR & (ADC_CR_ADSTART | ADC_CR_JADSTART | ADC_CR_ADCAL)) != 0U || !due(now)) return;
  uint32_t raw = 0U, ref = 0U;
  LL_ADC_SetCommonPathInternalChAdd(ADC12_COMMON, LL_ADC_PATH_INTERNAL_TEMPSENSOR | LL_ADC_PATH_INTERNAL_VREFINT);
  LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_TEMPSENSOR_ADC1, LL_ADC_SAMPLINGTIME_640CYCLES_5);
  LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_VREFINT, LL_ADC_SAMPLINGTIME_640CYCLES_5);
  volatile uint32_t delay = (SystemCoreClock / 1000000U + 1U) * LL_ADC_DELAY_TEMPSENSOR_STAB_US;
  while (delay--) __NOP();
  if (!LL_ADC_IsEnabled(ADC1)) {
    LL_ADC_ClearFlag_ADRDY(ADC1);
    LL_ADC_Enable(ADC1);
    if (!wait_bits(&ADC1->ISR, ADC_ISR_ADRDY, true)) { publish(0U, 0U, now); return; }
  }
  /* A two-rank software injected context; no external trigger or injected IRQ.
   * Only internal SMPR bits and JSQR change, never regular sequence registers. */
  ADC1->JSQR = 1U | (16U << ADC_JSQR_JSQ1_Pos) | (18U << ADC_JSQR_JSQ2_Pos);
  LL_ADC_ClearFlag_JEOS(ADC1);
  LL_ADC_INJ_StartConversion(ADC1);
  if (wait_bits(&ADC1->ISR, ADC_ISR_JEOS, true) && wait_bits(&ADC1->CR, ADC_CR_JADSTART, false)) {
    raw = ADC1->JDR1; ref = ADC1->JDR2;
  }
  publish(raw, ref, now);
}
