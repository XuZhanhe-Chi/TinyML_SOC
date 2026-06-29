#include "gpio.h"

enum {
  GPIO_REG_VALUE = 0x00,
  GPIO_REG_WRITE = 0x04,
  GPIO_REG_WRITE_ENABLE = 0x08,
};

static inline volatile uint32_t *gpio_reg(gpio_t gpio, uint32_t off) {
  return (volatile uint32_t *)(gpio.base + off);
}

uint32_t gpio_read(gpio_t gpio) { return *gpio_reg(gpio, GPIO_REG_VALUE); }
void gpio_write(gpio_t gpio, uint32_t value) { *gpio_reg(gpio, GPIO_REG_WRITE) = value; }
void gpio_set_output_enable(gpio_t gpio, uint32_t mask) { *gpio_reg(gpio, GPIO_REG_WRITE_ENABLE) = mask; }
