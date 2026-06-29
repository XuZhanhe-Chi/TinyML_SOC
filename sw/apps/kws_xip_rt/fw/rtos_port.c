#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"

#include "core/riscv.h"
#include "core/soc.h"
#include "drivers/timer/murax_timer.h"

extern void freertos_risc_v_trap_handler(void);

static void rtos_timer_config(uint32_t tick_cycles) {
  const murax_timer_t tim = murax_timer_from_base(SOC_TIMER_BASE);
  if (tick_cycles == 0u) {
    tick_cycles = 1u;
  }

  uint32_t div = (tick_cycles + 0xFFFFu - 1u) / 0xFFFFu;
  if (div == 0u) {
    div = 1u;
  }
  if (div > 0x10000u) {
    div = 0x10000u;
  }

  uint32_t prescaler_limit = div - 1u;
  uint32_t limit = (tick_cycles + div - 1u) / div;
  if (limit == 0u) {
    limit = 1u;
  }
  if (limit > 0x10000u) {
    limit = 0x10000u;
  }

  murax_timer_set_prescaler(tim, (uint16_t)prescaler_limit);
  /* Enable periodic tick: count on prescaler overflow, auto-clear on full. */
  murax_timer_set_ctrl_a(tim, 0x0002u, 0x0001u);
  murax_timer_set_limit_a(tim, (uint16_t)(limit - 1u));
  murax_timer_clear_a(tim);
  murax_timer_irq_clear(tim, 0x00000001u);
  murax_timer_irq_set_mask(tim, 0x00000001u);
}

void vPortSetupTimerInterrupt(void) {
  extern size_t xCriticalNesting;
  const uint32_t tick_cycles = (uint32_t)(configCPU_CLOCK_HZ / configTICK_RATE_HZ);
  rtos_timer_config(tick_cycles);
  riscv_csr_write_mtvec((uint32_t)(uintptr_t)freertos_risc_v_trap_handler);
  __asm__ volatile("csrs mie, %0" :: "r"(0x80u));
  xCriticalNesting = 0;
}

void freertos_risc_v_application_interrupt_handler(void) {
  const murax_timer_t tim = murax_timer_from_base(SOC_TIMER_BASE);
  const uint32_t pending = murax_timer_irq_pending_raw(tim);
  if ((pending & 0x00000001u) == 0u) {
    return;
  }

  murax_timer_irq_clear(tim, 0x00000001u);
  __asm__ volatile("fence iorw, iorw");
  (void)murax_timer_irq_pending_raw(tim);

  BaseType_t xHigherPriorityTaskWoken = xTaskIncrementTick();
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
