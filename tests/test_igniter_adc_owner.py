"""Actual ADC owner/task with inert HAL boundaries; no output commands."""
from pathlib import Path
import os, re, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
def function(source,name):
 m=re.search(r'(?m)^[A-Za-z_][^;\n]*\b'+name+r'\([^;]*?\)\s*\n\{',source);assert m,name
 start=m.start();end=m.end();depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]+'\n'
PRE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include "igniter_sense_math.h"
typedef unsigned UINT;typedef unsigned long ULONG;typedef int TX_THREAD;typedef unsigned TX_SEMAPHORE;
typedef int HAL_StatusTypeDef;typedef int HAL_DMA_StateTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define TX_SUCCESS 0U
#define TX_NOT_DONE 1U
#define TX_NO_INSTANCE 2U
#define TX_NO_WAIT 0U
#define TX_TIMER_TICKS_PER_SECOND 1000U
#define HAL_ADC_STATE_REG_BUSY 1U
#define HAL_DMA_STATE_BUSY 1U
#define DMA1_Channel1_IRQn 11
#define ADC_CR_ADSTART 1U
#define ADC_CR_JADSTART 2U
#define DMA_CCR_EN 1U
#define DMA_IT_TC 2U
#define DMA_IT_HT 4U
#define DMA_IT_TE 8U
#define __NVIC_PRIO_BITS 4
#define BOARD_WATCHDOG_SAFETY 3
#define SAFETY_ABORT_ADC_START 9
struct adc_regs {uint32_t CR;};struct dma_regs {uintptr_t CMAR;uint32_t CCR,CNDTR;};
struct dma_handle {struct dma_regs*Instance;unsigned State,ErrorCode;};
typedef struct {struct adc_regs*Instance;struct dma_handle*DMA_Handle;} ADC_HandleTypeDef;
static struct adc_regs adc,adc2={0xfeed};static struct dma_regs dma;
static struct dma_handle hdma={&dma,0,0};ADC_HandleTypeDef hadc1={&adc,&hdma};
#define ADC1 (&adc)
#define DMA1_Channel1 (&dma)
static unsigned tick,mask,basepri,irq_enabled=1,pending_irq,flags,starts,stops,clear_order,stop_fail,start_fail,complete_on_start,allowed=1,processes,abort_requests,temps,heartbeat_calls;
static unsigned flip_on_start,flip_on_get,inject_stop_error;
static unsigned captures,source_tick,source_raw,measurement_fault;static jmp_buf task_done;
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef*);void HAL_ADC_ErrorCallback(ADC_HandleTypeDef*);
uint32_t HAL_GetTick(void){return tick;}
uint32_t __get_PRIMASK(void){return mask;}void __disable_irq(void){mask=1;}void __set_PRIMASK(uint32_t n){mask=n;}
uint32_t __get_BASEPRI(void){return basepri;}void __set_BASEPRI(uint32_t n){basepri=n;}
void __DSB(void){}void __ISB(void){}void __DMB(void){}
void HAL_NVIC_DisableIRQ(int irq){assert(irq==11&&!mask);irq_enabled=0;}
void HAL_NVIC_ClearPendingIRQ(int irq){assert(irq==11&&!mask&&!irq_enabled);pending_irq=0;clear_order=1;}
void HAL_NVIC_EnableIRQ(int irq){assert(irq==11&&mask&&!pending_irq&&!flags&&clear_order);irq_enabled=1;}
#define __HAL_DMA_DISABLE(h) do{assert(!irq_enabled&&!mask);(h)->Instance->CCR&=~DMA_CCR_EN;}while(0)
#define __HAL_DMA_DISABLE_IT(h,b) do{assert(!irq_enabled&&!mask);(h)->Instance->CCR&=~(b);}while(0)
#define __HAL_DMA_CLEAR_FLAG(h,b) do{(void)(h);assert(!irq_enabled&&!mask);flags&=~(b);}while(0)
#define __HAL_DMA_GET_GI_FLAG_INDEX(h) ((void)(h),15U)
#define __HAL_DMA_GET_COUNTER(h) ((h)->Instance->CNDTR)
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef*h){assert(h==&hadc1&&!mask&&!irq_enabled&&!(dma.CCR&DMA_CCR_EN));++stops;if(inject_stop_error){inject_stop_error=0;HAL_ADC_ErrorCallback(h);}if(stop_fail)return HAL_ERROR;adc.CR=0;hdma.State=0;return HAL_OK;}
unsigned HAL_ADC_GetState(ADC_HandleTypeDef*h){(void)h;return adc.CR?1:0;}unsigned HAL_ADC_GetError(ADC_HandleTypeDef*h){(void)h;return 0;}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef*h,uint32_t*buffer,unsigned count){assert(h==&hadc1&&count==3&&!mask&&irq_enabled&&!pending_irq&&!flags);++starts;if(flip_on_start)allowed=0;if(start_fail)return HAL_ERROR;
 dma.CMAR=(uintptr_t)buffer;dma.CNDTR=count;dma.CCR|=DMA_CCR_EN;hdma.State=1;adc.CR=1;
 if(complete_on_start){((uint16_t*)buffer)[2]=2048;dma.CNDTR=0;hdma.State=0;adc.CR=0;HAL_ADC_ConvCpltCallback(h);}return HAL_OK;}
void igniter_sense_capture(uint16_t raw,uint32_t at){++captures;source_raw=raw;source_tick=at;measurement_fault=0;}
void igniter_sense_fault(void){measurement_fault=1;}
UINT tx_semaphore_put(TX_SEMAPHORE*s){++*s;return TX_SUCCESS;}
UINT tx_semaphore_get(TX_SEMAPHORE*s,unsigned timeout){if(timeout&&flip_on_get)allowed=0;if(*s){--*s;return TX_SUCCESS;}return TX_NO_INSTANCE;}
ULONG tx_time_get(void){return tick;}void tx_thread_sleep(unsigned n){(void)n;longjmp(task_done,1);}
unsigned thread_comm_abort_allowed(void){return allowed;}
uint64_t thread_comm_get_groundstation_heartbeat_ms(void){return 17;}
uint8_t thread_comm_get_flight_state(void){return allowed?2:5;}
uint64_t telemetry_event_clock_snapshot(void){return 1700000000000ULL+tick;}
int thread_comm_request_local_abort(unsigned reason,uint64_t at,uint64_t hb,uint8_t state,unsigned step,uint64_t utc,unsigned kind){assert(allowed&&reason==9&&at==tick&&hb==17&&state==2&&step==5&&utc==1700000000000ULL+tick&&kind==0);++abort_requests;return 1;}
void board_watchdog_progress(unsigned id){assert(id==3);}
void safety_check_heartbeat(void){++heartbeat_calls;}
void board_temperature_poll(void){++temps;assert(!adc.CR);}
'''
MAIN=r'''
int main(int argc,char**argv){assert(argc==2);unsigned scenario=(unsigned)strtoul(argv[1],0,10);tick=100;
 if(scenario==11){struct dma_regs other_dma={0};hdma.Instance=&other_dma;assert(safety_adc_start()!=TX_SUCCESS&&measurement_fault&&!starts&&irq_enabled);return 0;}
 if(scenario==10){hadc1.DMA_Handle=0;assert(safety_adc_start()!=TX_SUCCESS&&measurement_fault&&!safety_adc_started&&safety_adc_needs_quiesce&&safety_adc_start_step==2);assert(!safety_adc_quiesce()&&!starts);return 0;}
 if(scenario==9){HAL_ADC_ConvCpltCallback(&hadc1);inject_stop_error=1;
  assert(safety_adc_start()!=TX_SUCCESS&&!starts&&!irq_enabled&&measurement_fault);
  assert(safety_adc_start()==TX_SUCCESS&&starts==1&&irq_enabled);return 0;}
 if(scenario>=3){allowed=scenario==3||scenario==5||scenario==7||scenario==8;complete_on_start=1;start_fail=scenario==5||scenario==6||scenario==7;flip_on_start=scenario==7;flip_on_get=scenario==8;
  if(!setjmp(task_done))safety_task_entry(0);
  assert(starts==1&&heartbeat_calls==1&&temps==1&&adc2.CR==0xfeed);
  if(scenario==3)assert(captures==1&&processes==1&&!abort_requests);
  if(scenario==4)assert(captures==1&&!processes&&!abort_requests);
  if(scenario==5)assert(!captures&&!processes&&!abort_requests&&g_safety_adc_warning_pending); /* start failure warns, never aborts */
  if(scenario==6||scenario==7)assert(!captures&&!processes&&!abort_requests);
  if(scenario==8)assert(captures==1&&!processes&&!abort_requests);
  return 0;}
 HAL_ADC_ConvCpltCallback(&hadc1);assert(!captures&&measurement_fault&&safety_adc_needs_quiesce);
 pending_irq=1;flags=15;safety_adc_dma_sem=9;stop_fail=scenario==1;
 if(scenario==1){assert(safety_adc_start()!=TX_SUCCESS&&!starts&&!irq_enabled);stop_fail=0;}
 assert(safety_adc_start()!=TX_SUCCESS&&!starts&&!irq_enabled&&safety_adc_dma_sem==1);
 assert(safety_adc_start()==TX_SUCCESS&&starts==1&&irq_enabled&&!pending_irq&&!flags&&!safety_adc_dma_sem);
 HAL_ADC_ErrorCallback(&hadc1);assert(!safety_adc_started&&measurement_fault&&!captures);
 pending_irq=1;flags=15;safety_adc_dma_sem=1;
 assert(safety_adc_start()==TX_SUCCESS&&starts==2&&!pending_irq&&!flags&&!safety_adc_dma_sem);
 HAL_ADC_ConvCpltCallback(&hadc1);assert(!captures&&measurement_fault&&safety_adc_needs_quiesce);
 assert(safety_adc_start()==TX_SUCCESS&&starts==3);
 safety_adc_dma_buffer[2]=2345;dma.CNDTR=0;hdma.State=0;adc.CR=0;tick=150;
 HAL_ADC_ConvCpltCallback(&hadc1);assert(captures==1&&source_raw==2345&&source_tick==150&&!measurement_fault);
 HAL_ADC_ConvCpltCallback(&hadc1);assert(captures==1&&source_tick==150&&measurement_fault);
 assert(safety_adc_start()==TX_SUCCESS);tick+=251;
 assert(safety_adc_start()==TX_SUCCESS&&starts==5);
 assert(adc2.CR==0xfeed&&stops>=5&&!mask);return 0;
}
'''
class AdcOwnership(unittest.TestCase):
 def test_actual_dma_owner_quiescence_and_all_state_task(self):
  s=(ROOT/'Core/Src/safety_task.c').read_text();globals=s[s.index('TX_THREAD safety_task_thread;'):s.index('static uint64_t safety_now_ms(')]
  methods=''.join(function(s,n)for n in ['safety_adc_quiesce','safety_adc_start','safety_adc_publish_frame','safety_adc_take_latest','safety_now_ms','safety_task_entry','HAL_ADC_ConvHalfCpltCallback','HAL_ADC_ConvCpltCallback','HAL_ADC_ErrorCallback'])
  program=PRE+globals+'\nvoid safety_process_adc_sample(const safety_adc_sample_t*s){assert(s&&allowed);++processes;}\n'+methods+MAIN
  with tempfile.TemporaryDirectory()as d:
   for label,flags in [('native',[]),('sanitized',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
    exe=Path(d)/label
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-parameter','-Wno-unused-function',*flags,'-I',str(ROOT/'Core/Inc'),'-x','c','-','-o',str(exe)],input=program,text=True,check=True)
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:abort_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1'
    for scenario in range(12):subprocess.run([str(exe),str(scenario)],check=True,env=env)
 def test_actual_hal_stop_is_bounded_and_does_not_reset_adc2(self):
  root=ROOT/'Drivers/STM32G4xx_HAL_Driver';source=(root/'Src/stm32g4xx_hal_adc.c').read_text();stop=function(source,'HAL_ADC_Stop_DMA')
  self.assertIn('ADC_ConversionStop(hadc, ADC_REGULAR_INJECTED_GROUP)',stop);self.assertIn('HAL_DMA_Abort(hadc->DMA_Handle)',stop)
  self.assertIn('ADC_STOP_CONVERSION_TIMEOUT',function(source,'ADC_ConversionStop'))
  self.assertRegex((root/'Inc/stm32g4xx_hal_adc.h').read_text(),r'ADC_STOP_CONVERSION_TIMEOUT\s+\(\s*5UL\)')
  helper=function((ROOT/'Core/Src/safety_task.c').read_text(),'safety_adc_quiesce')
  self.assertNotIn('FORCE_RESET',helper);self.assertNotIn('ADC2->',helper);self.assertIn('DMA1_Channel1_IRQn',helper)
if __name__=='__main__':unittest.main()
