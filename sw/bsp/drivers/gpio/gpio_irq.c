#include "gpio_irq.h"

enum {
  GPIO_IRQ_REG_ENABLE = 0x00,
  GPIO_IRQ_REG_RISE = 0x04,
  GPIO_IRQ_REG_FALL = 0x08,
  GPIO_IRQ_REG_STATUS = 0x0C,
};

static inline volatile uint32_t *gpio_irq_reg(gpio_irq_t dev, uint32_t off) {
  return (volatile uint32_t *)(dev.base + off);
}

void gpio_irq_set_enable(gpio_irq_t dev, uint32_t mask) { *gpio_irq_reg(dev, GPIO_IRQ_REG_ENABLE) = mask; }
void gpio_irq_set_rise(gpio_irq_t dev, uint32_t mask) { *gpio_irq_reg(dev, GPIO_IRQ_REG_RISE) = mask; }
void gpio_irq_set_fall(gpio_irq_t dev, uint32_t mask) { *gpio_irq_reg(dev, GPIO_IRQ_REG_FALL) = mask; }
uint32_t gpio_irq_get_status(gpio_irq_t dev) { return *gpio_irq_reg(dev, GPIO_IRQ_REG_STATUS); }
void gpio_irq_clear(gpio_irq_t dev, uint32_t mask) { *gpio_irq_reg(dev, GPIO_IRQ_REG_STATUS) = mask; }
