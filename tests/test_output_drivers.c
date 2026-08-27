#include "igniter_driver.h"
#include "solenoid_driver.h"
#include <assert.h>

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) port->output |= pin;
    else port->output &= ~(uint32_t)pin;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return (port->input & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

int main(void)
{
    GPIO_TypeDef enable = {0};
    GPIO_TypeDef signal = {0};
    GPIO_TypeDef fault = {0};
    const uint16_t en_pin = 1U << 1;
    const uint16_t sig_pin = 1U << 2;
    const uint16_t fault_pin = 1U << 3;

    solenoid_t solenoid = {
        .en_port = &enable, .en_pin = en_pin,
        .sig_port = &signal, .sig_pin = sig_pin,
        .fault_port = &fault, .fault_pin = fault_pin,
    };
    solenoidInit(&solenoid);
    assert((enable.output & en_pin) == 0U && (signal.output & sig_pin) == 0U);
    assert(solenoidOn(&solenoid) == -1);
    fault.input |= fault_pin;
    assert(solenoidOn(&solenoid) == 0);
    assert((enable.output & en_pin) != 0U && (signal.output & sig_pin) != 0U);
    solenoidOff(&solenoid);
    assert((enable.output & en_pin) == 0U && (signal.output & sig_pin) == 0U);

    igniter_t igniter = {
        .en_port = &enable, .en_pin = en_pin,
        .fault_port = &fault, .fault_pin = fault_pin,
    };
    fault.input &= ~(uint32_t)fault_pin;
    igniterInit(&igniter);
    assert(igniterOn(&igniter) == -1);
    fault.input |= fault_pin;
    assert(igniterOn(&igniter) == 0);
    assert((enable.output & en_pin) != 0U);
    igniterOff(&igniter);
    assert((enable.output & en_pin) == 0U);
    return 0;
}
