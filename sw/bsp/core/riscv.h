#ifndef SW_VEXRISCV_BSP_CORE_RISCV_H_
#define SW_VEXRISCV_BSP_CORE_RISCV_H_

#include <stdint.h>

// CSR bits
#define RISCV_MSTATUS_MIE (1u << 3)
#define RISCV_MIE_MEIE (1u << 11)
#define RISCV_MIE_MTIE (1u << 7)

static inline uint32_t riscv_csr_read_mcause(void) {
  uint32_t v;
  __asm__ volatile("csrr %0, mcause" : "=r"(v));
  return v;
}

static inline uint32_t riscv_csr_read_mstatus(void) {
  uint32_t v;
  __asm__ volatile("csrr %0, mstatus" : "=r"(v));
  return v;
}

static inline uint32_t riscv_csr_read_mie(void) {
  uint32_t v;
  __asm__ volatile("csrr %0, mie" : "=r"(v));
  return v;
}

static inline uint32_t riscv_csr_read_mtvec(void) {
  uint32_t v;
  __asm__ volatile("csrr %0, mtvec" : "=r"(v));
  return v;
}

static inline uint32_t riscv_csr_read_mcycle(void) {
  uint32_t v;
  __asm__ volatile("csrr %0, mcycle" : "=r"(v));
  return v;
}

static inline void riscv_csr_write_mtvec(uint32_t v) { __asm__ volatile("csrw mtvec, %0" ::"r"(v)); }
static inline void riscv_csr_set_mie(uint32_t mask) { __asm__ volatile("csrs mie, %0" ::"r"(mask)); }
static inline void riscv_csr_set_mstatus(uint32_t mask) { __asm__ volatile("csrs mstatus, %0" ::"r"(mask)); }
static inline uint32_t riscv_csr_clear_mstatus(uint32_t mask) {
  uint32_t prev;
  __asm__ volatile("csrrc %0, mstatus, %1" : "=r"(prev) : "r"(mask));
  return prev;
}
static inline void riscv_csr_write_mstatus(uint32_t v) { __asm__ volatile("csrw mstatus, %0" ::"r"(v)); }

#endif  // SW_VEXRISCV_BSP_CORE_RISCV_H_
