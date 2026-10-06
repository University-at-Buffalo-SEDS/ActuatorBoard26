#pragma once
#define __DMB() __asm__ __volatile__("" ::: "memory")

extern unsigned mock_irq;
static inline unsigned __get_PRIMASK(void) { return mock_irq; }
static inline void __disable_irq(void) { mock_irq = 1U; }
static inline void __set_PRIMASK(unsigned saved) { mock_irq = saved; }
