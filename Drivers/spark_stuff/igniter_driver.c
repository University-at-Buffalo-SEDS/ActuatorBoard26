#include "igniter_driver.h"

void igniterInit(igniter_t *hw)
{
    HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_RESET);
}

int igniterOn(igniter_t *hw)
{
    if (igniterFault(hw)) {
        return -1;
    }

    HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);

    return 0;
}

void igniterOff(igniter_t *hw)
{
    HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_RESET);
}

bool igniterFault(igniter_t *hw)
{
    return HAL_GPIO_ReadPin(hw->fault_port, hw->fault_pin) == GPIO_PIN_RESET;
}