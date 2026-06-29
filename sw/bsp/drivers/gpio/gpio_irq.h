#ifndef SW_VEXRISCV_BSP_DRIVERS_GPIO_GPIO_IRQ_H_
#define SW_VEXRISCV_BSP_DRIVERS_GPIO_GPIO_IRQ_H_

#include <stdint.h>

typedef struct {
  uintptr_t base;
} gpio_irq_t;

static inline gpio_irq_t gpio_irq_from_base(uintptr_t base) { return (gpio_irq_t){.base = base}; }

void gpio_irq_set_enable(gpio_irq_t dev, uint32_t mask);
void gpio_irq_set_rise(gpio_irq_t dev, uint32_t mask);
void gpio_irq_set_fall(gpio_irq_t dev, uint32_t mask);
uint32_t gpio_irq_get_status(gpio_irq_t dev);
void gpio_irq_clear(gpio_irq_t dev, uint32_t mask);

#endif // SW_VEXRISCV_BSP_DRIVERS_GPIO_GPIO_IRQ_H_
