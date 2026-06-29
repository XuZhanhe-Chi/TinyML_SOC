#ifndef SW_VEXRISCV_BSP_DRIVERS_IRQ_IRQ_CTRL_H_
#define SW_VEXRISCV_BSP_DRIVERS_IRQ_IRQ_CTRL_H_

#include <stdint.h>

typedef struct {
  uintptr_t base;
} irq_ctrl_t;

static inline irq_ctrl_t irq_ctrl_from_base(uintptr_t base) { return (irq_ctrl_t){.base = base}; }

void irq_ctrl_set_enable(irq_ctrl_t dev, uint32_t mask);
uint32_t irq_ctrl_get_pending(irq_ctrl_t dev);
uint32_t irq_ctrl_get_raw(irq_ctrl_t dev);
void irq_ctrl_clear_pending(irq_ctrl_t dev, uint32_t mask);

#endif // SW_VEXRISCV_BSP_DRIVERS_IRQ_IRQ_CTRL_H_
