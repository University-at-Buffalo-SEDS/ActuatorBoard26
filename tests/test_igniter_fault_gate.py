"""Execute the production igniter GPIO gate; no physical load/key-switch claim."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'), 'Host C compiler required')
class IgniterFaultGateTests(unittest.TestCase):
    def test_fault_low_rejects_enable_high_enables_and_off_always_resets(self):
        with tempfile.TemporaryDirectory(prefix='igniter-driver-contract-') as directory:
            temp = Path(directory)
            (temp/'stm32g4xx_hal.h').write_text('''
#pragma once
#include <stdint.h>
typedef struct { unsigned id; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET=0, GPIO_PIN_SET=1 } GPIO_PinState;
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
''')
            (temp/'contract.c').write_text('''
#include <assert.h>
#include "igniter_driver.h"
static GPIO_TypeDef enable_port={1},fault_port={2};
static unsigned writes;
static GPIO_PinState enabled=GPIO_PIN_SET,fault=GPIO_PIN_RESET;
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state) {
    assert(port==&enable_port && pin==(1u<<7));
    writes++;enabled=state;
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin) {
    assert(port==&fault_port && pin==(1u<<3));return fault;
}
int main(void) {
    igniter_t device={.en_port=&enable_port,.en_pin=1u<<7,
        .fault_port=&fault_port,.fault_pin=1u<<3};
    igniterInit(&device);assert(writes==1 && enabled==GPIO_PIN_RESET);
    writes=0;assert(igniterFault(&device));
    assert(igniterOn(&device)==-1 && writes==0 && enabled==GPIO_PIN_RESET);
    fault=GPIO_PIN_SET;assert(!igniterFault(&device));
    assert(igniterOn(&device)==0 && writes==1 && enabled==GPIO_PIN_SET);
    // Fault gates a subsequent On call; this function is not an autonomous cutoff.
    fault=GPIO_PIN_RESET;writes=0;
    assert(igniterOn(&device)==-1 && writes==0);
    igniterOff(&device);assert(writes==1 && enabled==GPIO_PIN_RESET);
    fault=GPIO_PIN_SET;writes=0;igniterOff(&device);
    assert(writes==1 && enabled==GPIO_PIN_RESET);
    return 0;
}
''')
            binary=temp/'igniter-contract'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(temp),'-I',str(ROOT/'Drivers/spark_stuff'),
                            str(ROOT/'Drivers/spark_stuff/igniter_driver.c'),
                            str(temp/'contract.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
