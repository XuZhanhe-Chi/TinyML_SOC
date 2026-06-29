#ifndef SW_VEXRISCV_BSP_DRIVERS_GPIO_GPIO_H_
#define SW_VEXRISCV_BSP_DRIVERS_GPIO_GPIO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uintptr_t base;
} gpio_t;

static inline gpio_t gpio_from_base(uintptr_t base) { return (gpio_t){.base = base}; }

uint32_t gpio_read(gpio_t gpio);
void gpio_write(gpio_t gpio, uint32_t value);
void gpio_set_output_enable(gpio_t gpio, uint32_t mask);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // SW_VEXRISCV_BSP_DRIVERS_GPIO_GPIO_H_
