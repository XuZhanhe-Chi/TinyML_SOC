#ifndef SW_VEXRISCV_BSP_DRIVERS_VENUS_VENUS_HW_H_
#define SW_VEXRISCV_BSP_DRIVERS_VENUS_VENUS_HW_H_

#include <stdint.h>

#include "core/soc.h"

// ==========================================================
// 1) SoC address map
// ==========================================================

// NPU APB base (from soc.h)
#define VENUS_REG_BASE SOC_NPU_APB_BASE

// NPU DMA visible memory window (default to SRAM)
#define VENUS_SHARED_BASE SOC_SRAM_BASE
#define VENUS_SHARED_HIGH (SOC_SRAM_BASE + SOC_SRAM_SIZE - 1u)

// ==========================================================
// 2) Register offsets
// ==========================================================

#define VENUS_REG_UOP_BASE 0x000u   // RW
#define VENUS_REG_CTRL 0x004u       // RW
#define VENUS_REG_STATUS 0x008u     // RO
#define VENUS_REG_VERSION 0x00Cu    // RO
#define VENUS_REG_UOP_COUNT 0x010u  // RW
#define VENUS_REG_INT_EN 0x020u     // RW
#define VENUS_REG_INT_STATUS 0x024u // RW1C
#define VENUS_REG_DEBUG0 0x080u     // RO
#define VENUS_REG_DEBUG1 0x084u     // RO
#define VENUS_REG_DEBUG2 0x088u     // RO
#define VENUS_REG_DEBUG3 0x08Cu     // RO
#define VENUS_REG_DEBUG4 0x090u     // RO
#define VENUS_REG_DEBUG5 0x094u     // RO
#define VENUS_REG_DEBUG6 0x098u     // RO
#define VENUS_REG_DEBUG7 0x09Cu     // RO
#define VENUS_REG_DEBUG8 0x0A0u     // RO
#define VENUS_REG_DEBUG_CTRL 0x0A4u // RW1P

// CTRL
#define CTRL_START_MASK (1u << 0)
#define CTRL_ABORT_MASK (1u << 1)
#define CTRL_RESET_MASK (1u << 2)

// STATUS
#define STATUS_BUSY_MASK (1u << 0)
#define STATUS_ERROR_MASK (1u << 1)

#define STATUS_CURR_OPCODE_SHIFT 4u
#define STATUS_CURR_OPCODE_MASK (0xFu << STATUS_CURR_OPCODE_SHIFT)

#define STATUS_ERROR_CODE_SHIFT 8u
#define STATUS_ERROR_CODE_MASK (0xFFu << STATUS_ERROR_CODE_SHIFT)

// INT
#define INT_DONE_MASK (1u << 0)
#define INT_ERROR_MASK (1u << 1)

// ==========================================================
// 3) uOP format
// ==========================================================

#define VENUS_UOP_WORDS 8u
#define VENUS_UOP_SIZE_BYTES (VENUS_UOP_WORDS * 4u)

typedef struct {
  uint32_t w0;
  uint32_t w1;
  uint32_t w2;
  uint32_t w3;
  uint32_t w4;
  uint32_t w5;
  uint32_t w6;
  uint32_t w7;
} __attribute__((packed, aligned(4))) venus_uop_t;

#endif  // SW_VEXRISCV_BSP_DRIVERS_VENUS_VENUS_HW_H_
