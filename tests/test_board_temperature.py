from pathlib import Path
import subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class TemperatureTests(unittest.TestCase):
 def test_factory_ratio_and_wire_backward_compatibility(self):
  c=r"""
#include "board_temperature_math.h"
#include <assert.h>
#include <stddef.h>
int main(void) {
 int16_t c=0;
 /* 12-bit factory codes; supply and measured resolution cancel in ratio. */
 assert(board_temperature_convert(1200,1500,1500,1200,1600,30,130,&c)&&c==3000);
 assert(board_temperature_convert(2800,3000,1500,1200,1600,30,130,&c)&&c==8000);
 assert(board_temperature_convert(1100,1500,1500,1200,1600,30,130,&c)&&c==500);
 /* U585 factory uses 14-bit calibration, same ratio at lower ADC scale. */
 assert(board_temperature_convert(2600,3000,6000,4800,6400,30,130,&c)&&c==5500);
 assert(!board_temperature_convert(1200,0,1500,1200,1600,30,130,&c));
 assert(!board_temperature_convert(1200,1500,1500,1600,1200,30,130,&c));
 assert(!board_temperature_convert(1200,1500,65535,1200,1600,30,130,&c));
 assert(!board_temperature_convert(4000,1500,1500,1200,1600,30,130,&c));
 assert(!board_temperature_convert(1200,1500,1500,1200,1600,30,130,NULL));
 uint32_t legacy=0xff, flags=legacy|board_temperature_pack(-1234,101);
 assert((flags&0xff)==legacy); /* every previous counter's validity unchanged */
 assert(flags&0x100);assert((int16_t)(flags>>16)==-1234);
 assert(((flags>>9)&127)==2); /* conservative age rounds upward */
 assert(board_temperature_pack(15000,6000));
 assert(!board_temperature_pack(15001,0));assert(!board_temperature_pack(2500,6001));
}
"""
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(c)
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(ROOT/'Core/Inc'),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)

 def test_safety_adc_injected_pair_never_preempts_regular_dma(self):
  source=(ROOT/'Core/Src/board_temperature.c').read_text()
  source='\n'.join(l for l in source.splitlines() if not l.startswith('#include'))
  c=r"""
#include "board_temperature_math.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t CR,ISR,JSQR,JDR1,JDR2,SQR1,CFGR,SMPR2;} ADC_TypeDef;
static ADC_TypeDef adc;
#define ADC1 (&adc)
#define ADC12_COMMON (&adc)
#define ADC_CR_ADSTART 1U
#define ADC_CR_JADSTART 2U
#define ADC_CR_ADCAL 4U
#define ADC_CR_ADEN 8U
#define ADC_ISR_ADRDY 1U
#define ADC_ISR_JEOS 2U
#define ADC_JSQR_JSQ1_Pos 8U
#define ADC_JSQR_JSQ2_Pos 14U
#define LL_ADC_PATH_INTERNAL_TEMPSENSOR 1U
#define LL_ADC_PATH_INTERNAL_VREFINT 2U
#define LL_ADC_CHANNEL_TEMPSENSOR_ADC1 16U
#define LL_ADC_CHANNEL_VREFINT 18U
#define LL_ADC_SAMPLINGTIME_640CYCLES_5 7U
#define LL_ADC_DELAY_TEMPSENSOR_STAB_US 120U
#define TEMPSENSOR_CAL1_TEMP 30
#define TEMPSENSOR_CAL2_TEMP 130
static const uint16_t vrefcal=1500,cal1=1200,cal2=1600;
#define VREFINT_CAL_ADDR (&vrefcal)
#define TEMPSENSOR_CAL1_ADDR (&cal1)
#define TEMPSENSOR_CAL2_ADDR (&cal2)
static uint32_t tick,mask,starts;
static bool fail;
uint32_t SystemCoreClock=1000;
uint32_t HAL_GetTick(void){return tick;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t m){mask=m;}
void __NOP(void){}
void LL_ADC_SetCommonPathInternalChAdd(ADC_TypeDef *a,uint32_t b){(void)a;assert(b==3);assert(!mask);}
void LL_ADC_SetChannelSamplingTime(ADC_TypeDef *a,uint32_t ch,uint32_t t){
 assert(ch==16||ch==18);assert(t==7);a->SMPR2|=t<<((ch-10)*3);assert(!mask);
}
uint32_t LL_ADC_IsEnabled(ADC_TypeDef *a){return a->CR&ADC_CR_ADEN;}
void LL_ADC_ClearFlag_ADRDY(ADC_TypeDef *a){a->ISR&=~ADC_ISR_ADRDY;}
void LL_ADC_Enable(ADC_TypeDef *a){a->CR|=ADC_CR_ADEN;a->ISR|=ADC_ISR_ADRDY;assert(!mask);}
void LL_ADC_ClearFlag_JEOS(ADC_TypeDef *a){a->ISR&=~ADC_ISR_JEOS;}
void LL_ADC_INJ_StartConversion(ADC_TypeDef *a){
 assert(!(a->CR&(ADC_CR_ADSTART|ADC_CR_JADSTART)));assert(!mask);starts++;
 if(!fail){a->JDR1=1400;a->JDR2=1500;a->ISR|=ADC_ISR_JEOS;}
}
"""+source+r"""
int main(void){
 adc.SQR1=0x12345678;adc.CFGR=0x87654321;adc.SMPR2=0x12345;
 adc.CR=ADC_CR_ADEN|ADC_CR_ADSTART;tick=10;board_temperature_poll();
 assert(!starts&&!board_temperature_health_flags()); // busy owner: no preemption
 adc.CR&=~ADC_CR_ADSTART;board_temperature_poll();
 assert(starts==1&&(board_temperature_health_flags()&0x100));
 assert((int16_t)(board_temperature_health_flags()>>16)==8000);
 assert(adc.SQR1==0x12345678&&adc.CFGR==0x87654321&&(adc.SMPR2&0x3ffff)==0x12345);
 tick=5009;board_temperature_poll();assert(starts==1);
 tick=5010;fail=true;board_temperature_poll();assert(starts==2&&!board_temperature_health_flags());
 tick=10010;fail=false;board_temperature_poll();assert(starts==3&&board_temperature_health_flags());
 tick+=6001;assert(!board_temperature_health_flags()); // stale, no fake fresh reading
 tick=UINT32_MAX-100;board_temperature_poll();assert(starts==4);
 tick+=5000;board_temperature_poll();assert(starts==5); // uptime wraps safely
}
"""
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(c)
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(ROOT/'Core/Inc'),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
