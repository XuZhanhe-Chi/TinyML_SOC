#ifndef SW_VEXRISCV_BSP_DRIVERS_VENUS_VENUS_DRIVER_H_
#define SW_VEXRISCV_BSP_DRIVERS_VENUS_VENUS_DRIVER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/venus/venus_hw.h"

// =============================================================================
// 1) Error codes
// =============================================================================

typedef enum {
  VENUS_OK = 0,

  // Software-side errors
  VENUS_ERR_INVALID_ARG = -1,
  VENUS_ERR_NO_MEM = -2,
  VENUS_ERR_BUNDLE_FORMAT = -3,
  VENUS_ERR_RELOC_UNSUPPORTED = -4,
  VENUS_ERR_ADDR_RANGE = -5,
  VENUS_ERR_ADDR_TRUNC = -6,

  // Runtime errors
  VENUS_ERR_TIMEOUT = -10,
  VENUS_ERR_HARDWARE = -11
} venus_status_t;

const char *venus_strerror(venus_status_t st);

// =============================================================================
// 2) Bundle description
// =============================================================================

typedef struct {
  const uint32_t *uops_words;
  size_t uops_len_words;
  const uint32_t *params_words;
  size_t params_len_words;

  uint32_t address_mode_offset; // 1=offset, 0=absolute

  uint32_t input_base;
  uint32_t input_size;
  uint32_t output_base;
  uint32_t output_size;

  uint32_t param_base;
  uint32_t param_block_size;
  uint32_t activation_peak_bytes;
} venus_bundle_t;

// Helper macro: requires bundle.h already included by caller.
#define VENUS_BUNDLE_FROM_BUNDLE_H()                                           \
  ((venus_bundle_t){                                                           \
      .uops_words = uops_words,                                                \
      .uops_len_words = uops_words_len_words,                                  \
      .params_words = params_words,                                            \
      .params_len_words = params_words_len_words,                              \
      .address_mode_offset = (uint32_t)ADDRESS_MODE_OFFSET,                    \
      .input_base = (uint32_t)INPUT_BASE,                                      \
      .input_size = (uint32_t)INPUT_SIZE,                                      \
      .output_base = (uint32_t)OUTPUT_BASE,                                    \
      .output_size = (uint32_t)OUTPUT_SIZE,                                    \
      .param_base = (uint32_t)PARAM_BASE,                                      \
      .param_block_size = (uint32_t)PARAM_BLOCK_SIZE,                          \
      .activation_peak_bytes = (uint32_t)ACTIVATION_PEAK_BYTES,                \
  })

// =============================================================================
// 3) Runtime memory description (physical)
// =============================================================================

typedef struct {
  void *uops_buf;
  uintptr_t uops_buf_pa;
  uint32_t uops_buf_bytes;

  void *params_buf;
  uintptr_t params_buf_pa;
  uint32_t params_buf_bytes;

  void *act_buf;
  uintptr_t act_base_pa;
  uint32_t act_buf_bytes;
} venus_mem_t;

void venus_mem_init(venus_mem_t *mem, void *uops_buf, uintptr_t uops_buf_pa,
                    uint32_t uops_buf_bytes, void *params_buf,
                    uintptr_t params_buf_pa, uint32_t params_buf_bytes,
                    void *act_buf, uintptr_t act_base_pa,
                    uint32_t act_buf_bytes);

// =============================================================================
// 4) Cache hooks (weak, platform may override)
// =============================================================================

void venus_cache_flush(const void *addr, size_t size);
void venus_cache_invalidate(void *addr, size_t size);

// =============================================================================
// 5) NPU control
// =============================================================================

void venus_init(void);
void venus_soft_reset(void);
void venus_abort(void);

venus_status_t venus_submit_and_start(uintptr_t uop_base_pa,
                                      uint32_t uop_count);
venus_status_t venus_wait_idle(uint32_t timeout_cycles, uint32_t *out_status);
venus_status_t venus_wait_irq(uint32_t timeout_cycles, uint32_t *out_status);

// Optional hook: reset/capture timing immediately before NPU start.
void venus_irq_mark_start(void);

// =============================================================================
// 6) One-shot run
// =============================================================================

venus_status_t venus_run_bundle(const venus_bundle_t *bundle,
                                const venus_mem_t *mem,
                                uint32_t timeout_cycles,
                                uint32_t *out_hw_status);

venus_status_t venus_run_bundle_irq(const venus_bundle_t *bundle,
                                    const venus_mem_t *mem,
                                    uint32_t timeout_cycles,
                                    uint32_t *out_hw_status);

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------
// Offset-mode uOP relocation helper.
//
// When `bundle.address_mode_offset!=0`, the uOP fields `PARAM_ADDR/FI_ADDR/FO_ADDR`
// are offsets and must be relocated to absolute byte addresses before submitting.
//
// This helper patches the uOP stream in-place:
//   - W3 (PARAM_ADDR) += param_base_pa
//   - W4 (FI_ADDR)    += act_base_pa
//   - W5 (FO_ADDR)    += act_base_pa
//
// Use case:
//   - Keep large `params_words` in QSPI/XIP (no copy), only copy+patch small uOPs into SRAM.
venus_status_t venus_patch_uops_offset_mode_inplace(void *uops_buf,
                                                    size_t uops_len_words,
                                                    uintptr_t act_base_pa,
                                                    uintptr_t param_base_pa);

#endif  // SW_VEXRISCV_BSP_DRIVERS_VENUS_VENUS_DRIVER_H_
