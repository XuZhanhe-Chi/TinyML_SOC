#include "irq_ctrl.h"

enum {
  IRQ_CTRL_REG_ENABLE = 0x00,
  IRQ_CTRL_REG_PENDING = 0x04,
  IRQ_CTRL_REG_RAW = 0x08,
};

static inline volatile uint32_t *irq_ctrl_reg(irq_ctrl_t dev, uint32_t off) {
  return (volatile uint32_t *)(dev.base + off);
}

void irq_ctrl_set_enable(irq_ctrl_t dev, uint32_t mask) { *irq_ctrl_reg(dev, IRQ_CTRL_REG_ENABLE) = mask; }
uint32_t irq_ctrl_get_pending(irq_ctrl_t dev) { return *irq_ctrl_reg(dev, IRQ_CTRL_REG_PENDING); }
uint32_t irq_ctrl_get_raw(irq_ctrl_t dev) { return *irq_ctrl_reg(dev, IRQ_CTRL_REG_RAW); }
void irq_ctrl_clear_pending(irq_ctrl_t dev, uint32_t mask) { *irq_ctrl_reg(dev, IRQ_CTRL_REG_PENDING) = mask; }
