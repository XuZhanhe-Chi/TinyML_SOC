#ifndef SW_VEXRISCV_BSP_DRIVERS_TIMER_MURAX_TIMER_H_
#define SW_VEXRISCV_BSP_DRIVERS_TIMER_MURAX_TIMER_H_

#include <stdint.h>

typedef struct {
  uintptr_t base;
} murax_timer_t;

static inline murax_timer_t murax_timer_from_base(uintptr_t base) { return (murax_timer_t){.base = base}; }

// Prescaler (offset 0x00): write limit also clears counter
void murax_timer_set_prescaler(murax_timer_t dev, uint16_t limit);

// Interrupt controller inside timer (offset 0x10)
uint32_t murax_timer_irq_pending_raw(murax_timer_t dev);
void murax_timer_irq_clear(murax_timer_t dev, uint32_t mask);
void murax_timer_irq_set_mask(murax_timer_t dev, uint32_t mask);

// TimerA @ 0x40, TimerB @ 0x50
void murax_timer_set_limit_a(murax_timer_t dev, uint16_t limit);
void murax_timer_set_limit_b(murax_timer_t dev, uint16_t limit);
uint32_t murax_timer_get_value_a(murax_timer_t dev);
uint32_t murax_timer_get_value_b(murax_timer_t dev);
void murax_timer_clear_a(murax_timer_t dev);
void murax_timer_clear_b(murax_timer_t dev);

// tick/clear enables at base + 0
void murax_timer_set_ctrl_a(murax_timer_t dev, uint16_t tick_mask, uint16_t clear_mask);
void murax_timer_set_ctrl_b(murax_timer_t dev, uint16_t tick_mask, uint16_t clear_mask);
void murax_timer_set_tick_mask_a(murax_timer_t dev, uint16_t mask);
void murax_timer_set_clear_mask_a(murax_timer_t dev, uint16_t mask);
void murax_timer_set_tick_mask_b(murax_timer_t dev, uint16_t mask);
void murax_timer_set_clear_mask_b(murax_timer_t dev, uint16_t mask);

// Helpers for simple tick counter on TimerA
void murax_timer_init_tick_a(murax_timer_t dev, uint16_t prescaler_limit);
uint32_t murax_timer_read_tick_a(murax_timer_t dev);
// Helpers for simple tick counter on TimerB
void murax_timer_init_tick_b(murax_timer_t dev, uint16_t prescaler_limit);
uint32_t murax_timer_read_tick_b(murax_timer_t dev);

#endif // SW_VEXRISCV_BSP_DRIVERS_TIMER_MURAX_TIMER_H_
