#include "murax_timer.h"

static inline volatile uint32_t *reg32(uintptr_t base, uint32_t off) { return (volatile uint32_t *)(base + off); }
static inline void io_fence(void) { __asm__ volatile("fence iorw, iorw"); }

enum {
  TIMER_PRESCALER_LIMIT = 0x00,

  TIMER_IRQ_PENDING_CLEAR = 0x10,
  TIMER_IRQ_MASK = 0x14,

  TIMERA_CTRL = 0x40,
  TIMERA_LIMIT = 0x44,
  TIMERA_VALUE = 0x48,

  TIMERB_CTRL = 0x50,
  TIMERB_LIMIT = 0x54,
  TIMERB_VALUE = 0x58,
};

static inline void timer_set_ctrl(uintptr_t base, uint32_t ctrl_off, uint16_t tick_mask, uint16_t clear_mask) {
  uint32_t v = ((uint32_t)tick_mask & 0xFFFFu) | (((uint32_t)clear_mask & 0xFFFFu) << 16);
  *reg32(base, ctrl_off) = v;
}

static inline void timer_write_ctrl(uintptr_t base, uint32_t ctrl_off, uint16_t tick_mask, uint16_t clear_mask) {
  uint32_t v = ((uint32_t)tick_mask & 0xFFFFu) | (((uint32_t)clear_mask & 0xFFFFu) << 16);
  *reg32(base, ctrl_off) = v;
  io_fence();
  (void)*reg32(base, ctrl_off);
}

void murax_timer_set_prescaler(murax_timer_t dev, uint16_t limit) {
  *reg32(dev.base, TIMER_PRESCALER_LIMIT) = limit;
  io_fence();
}

uint32_t murax_timer_irq_pending_raw(murax_timer_t dev) { return *reg32(dev.base, TIMER_IRQ_PENDING_CLEAR); }
void murax_timer_irq_clear(murax_timer_t dev, uint32_t mask) { *reg32(dev.base, TIMER_IRQ_PENDING_CLEAR) = mask; }
void murax_timer_irq_set_mask(murax_timer_t dev, uint32_t mask) { *reg32(dev.base, TIMER_IRQ_MASK) = mask; }

void murax_timer_set_limit_a(murax_timer_t dev, uint16_t limit) { *reg32(dev.base, TIMERA_LIMIT) = limit; }
void murax_timer_set_limit_b(murax_timer_t dev, uint16_t limit) { *reg32(dev.base, TIMERB_LIMIT) = limit; }

uint32_t murax_timer_get_value_a(murax_timer_t dev) { return *reg32(dev.base, TIMERA_VALUE); }
uint32_t murax_timer_get_value_b(murax_timer_t dev) { return *reg32(dev.base, TIMERB_VALUE); }

void murax_timer_clear_a(murax_timer_t dev) {
  *reg32(dev.base, TIMERA_VALUE) = 0;
  io_fence();
  (void)*reg32(dev.base, TIMERA_VALUE);
}
void murax_timer_clear_b(murax_timer_t dev) {
  *reg32(dev.base, TIMERB_VALUE) = 0;
  io_fence();
  (void)*reg32(dev.base, TIMERB_VALUE);
}

void murax_timer_set_ctrl_a(murax_timer_t dev, uint16_t tick_mask, uint16_t clear_mask) {
  timer_write_ctrl(dev.base, TIMERA_CTRL, tick_mask, clear_mask);
}

void murax_timer_set_ctrl_b(murax_timer_t dev, uint16_t tick_mask, uint16_t clear_mask) {
  timer_write_ctrl(dev.base, TIMERB_CTRL, tick_mask, clear_mask);
}

void murax_timer_set_tick_mask_a(murax_timer_t dev, uint16_t mask) { timer_set_ctrl(dev.base, TIMERA_CTRL, mask, (uint16_t)(*reg32(dev.base, TIMERA_CTRL) >> 16)); }
void murax_timer_set_clear_mask_a(murax_timer_t dev, uint16_t mask) { timer_set_ctrl(dev.base, TIMERA_CTRL, (uint16_t)(*reg32(dev.base, TIMERA_CTRL)), mask); }
void murax_timer_set_tick_mask_b(murax_timer_t dev, uint16_t mask) { timer_set_ctrl(dev.base, TIMERB_CTRL, mask, (uint16_t)(*reg32(dev.base, TIMERB_CTRL) >> 16)); }
void murax_timer_set_clear_mask_b(murax_timer_t dev, uint16_t mask) { timer_set_ctrl(dev.base, TIMERB_CTRL, (uint16_t)(*reg32(dev.base, TIMERB_CTRL)), mask); }

void murax_timer_init_tick_a(murax_timer_t dev, uint16_t prescaler_limit) {
  murax_timer_set_prescaler(dev, prescaler_limit);
  // tick source: prescaler overflow => bit1
  murax_timer_set_ctrl_a(dev, 0x0002u, 0x0000u);
  murax_timer_set_limit_a(dev, 0xFFFFu);
  murax_timer_clear_a(dev);
  (void)murax_timer_get_value_a(dev);
  (void)murax_timer_get_value_a(dev);
}

uint32_t murax_timer_read_tick_a(murax_timer_t dev) {
  return murax_timer_get_value_a(dev) & 0xFFFFu;
}

void murax_timer_init_tick_b(murax_timer_t dev, uint16_t prescaler_limit) {
  (void)prescaler_limit;
  // tick source: prescaler overflow => bit1
  murax_timer_set_ctrl_b(dev, 0x0002u, 0x0000u);
  murax_timer_set_limit_b(dev, 0xFFFFu);
  murax_timer_clear_b(dev);
  (void)murax_timer_get_value_b(dev);
  (void)murax_timer_get_value_b(dev);
}

uint32_t murax_timer_read_tick_b(murax_timer_t dev) {
  return murax_timer_get_value_b(dev) & 0xFFFFu;
}
