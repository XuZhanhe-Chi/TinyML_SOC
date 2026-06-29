#include "drivers/venus/venus_driver.h"

#include <string.h>

// =============================================================================
// 0) MMIO helpers
// =============================================================================

#define VENUS_MMIO32(addr) (*((volatile uint32_t *)(uintptr_t)(addr)))

static inline void venus_reg_write(uint32_t reg_off, uint32_t v) {
  VENUS_MMIO32(VENUS_REG_BASE + reg_off) = v;
}

static inline uint32_t venus_reg_read(uint32_t reg_off) {
  return VENUS_MMIO32(VENUS_REG_BASE + reg_off);
}

// =============================================================================
// 1) Error strings
// =============================================================================

const char *venus_strerror(venus_status_t st) {
  switch (st) {
  case VENUS_OK:
    return "VENUS_OK";
  case VENUS_ERR_INVALID_ARG:
    return "VENUS_ERR_INVALID_ARG";
  case VENUS_ERR_NO_MEM:
    return "VENUS_ERR_NO_MEM";
  case VENUS_ERR_BUNDLE_FORMAT:
    return "VENUS_ERR_BUNDLE_FORMAT";
  case VENUS_ERR_RELOC_UNSUPPORTED:
    return "VENUS_ERR_RELOC_UNSUPPORTED";
  case VENUS_ERR_ADDR_RANGE:
    return "VENUS_ERR_ADDR_RANGE";
  case VENUS_ERR_ADDR_TRUNC:
    return "VENUS_ERR_ADDR_TRUNC";
  case VENUS_ERR_TIMEOUT:
    return "VENUS_ERR_TIMEOUT";
  case VENUS_ERR_HARDWARE:
    return "VENUS_ERR_HARDWARE";
  default:
    return "VENUS_ERR_UNKNOWN";
  }
}

// =============================================================================
// 2) Cache hooks (weak no-op)
// =============================================================================

#if defined(__GNUC__)
__attribute__((weak))
#endif
void venus_cache_flush(const void *addr, size_t size) {
  (void)addr;
  (void)size;
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

#if defined(__GNUC__)
__attribute__((weak))
#endif
void venus_cache_invalidate(void *addr, size_t size) {
  (void)addr;
  (void)size;
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

// =============================================================================
// 3) Runtime memory
// =============================================================================

void venus_mem_init(venus_mem_t *mem, void *uops_buf, uintptr_t uops_buf_pa,
                    uint32_t uops_buf_bytes, void *params_buf,
                    uintptr_t params_buf_pa, uint32_t params_buf_bytes,
                    void *act_buf, uintptr_t act_base_pa,
                    uint32_t act_buf_bytes) {
  if (!mem) {
    return;
  }
  mem->uops_buf = uops_buf;
  mem->uops_buf_pa = uops_buf_pa;
  mem->uops_buf_bytes = uops_buf_bytes;
  mem->params_buf = params_buf;
  mem->params_buf_pa = params_buf_pa;
  mem->params_buf_bytes = params_buf_bytes;
  mem->act_buf = act_buf;
  mem->act_base_pa = act_base_pa;
  mem->act_buf_bytes = act_buf_bytes;
}

// =============================================================================
// 4) NPU control
// =============================================================================

void venus_init(void) {
  venus_reg_write(VENUS_REG_CTRL, CTRL_RESET_MASK);
  for (volatile uint32_t i = 0; i < 200u; ++i) {
    __asm__ volatile("" ::: "memory");
  }
  venus_reg_write(VENUS_REG_INT_EN, 0u);
  venus_reg_write(VENUS_REG_INT_STATUS, (INT_DONE_MASK | INT_ERROR_MASK));
}

void venus_soft_reset(void) { venus_reg_write(VENUS_REG_CTRL, CTRL_RESET_MASK); }

void venus_abort(void) { venus_reg_write(VENUS_REG_CTRL, CTRL_ABORT_MASK); }

venus_status_t venus_submit_and_start(uintptr_t uop_base_pa,
                                      uint32_t uop_count) {
  if (uop_count == 0u) {
    return VENUS_ERR_INVALID_ARG;
  }
  if (sizeof(uintptr_t) > 4u && uop_base_pa > 0xFFFFFFFFu) {
    return VENUS_ERR_ADDR_TRUNC;
  }
  venus_reg_write(VENUS_REG_INT_STATUS, (INT_DONE_MASK | INT_ERROR_MASK));
  venus_reg_write(VENUS_REG_UOP_BASE, (uint32_t)uop_base_pa);
  venus_reg_write(VENUS_REG_UOP_COUNT, uop_count);
  venus_irq_mark_start();
  venus_reg_write(VENUS_REG_CTRL, CTRL_START_MASK);
  return VENUS_OK;
}

venus_status_t venus_wait_idle(uint32_t timeout_cycles, uint32_t *out_status) {
  uint32_t status = 0;
  for (uint32_t i = 0; i < timeout_cycles; ++i) {
    status = venus_reg_read(VENUS_REG_STATUS);
    if (status & STATUS_ERROR_MASK) {
      if (out_status) {
        *out_status = status;
      }
      return VENUS_ERR_HARDWARE;
    }
    if ((status & STATUS_BUSY_MASK) == 0u) {
      if (out_status) {
        *out_status = status;
      }
      return VENUS_OK;
    }
  }
  if (out_status) {
    *out_status = status;
  }
  return VENUS_ERR_TIMEOUT;
}

// Optional IRQ wait hook (override in application)
#if defined(__GNUC__)
__attribute__((weak))
#endif
int venus_irq_wait(uint32_t timeout_cycles) {
  (void)timeout_cycles;
  return 0;
}

venus_status_t venus_wait_irq(uint32_t timeout_cycles, uint32_t *out_status) {
  if (!venus_irq_wait(timeout_cycles)) {
    if (out_status) {
      *out_status = venus_reg_read(VENUS_REG_STATUS);
    }
    return VENUS_ERR_TIMEOUT;
  }

  uint32_t status = venus_reg_read(VENUS_REG_STATUS);
  if (out_status) {
    *out_status = status;
  }
  if (status & STATUS_ERROR_MASK) {
    return VENUS_ERR_HARDWARE;
  }
  if (status & STATUS_BUSY_MASK) {
    return VENUS_ERR_TIMEOUT;
  }
  return VENUS_OK;
}

// Optional hook (override in application)
#if defined(__GNUC__)
__attribute__((weak))
#endif
void venus_irq_mark_start(void) {}

// =============================================================================
// 5) Internal helpers
// =============================================================================

static inline void write_le_u32(uint8_t *dst4, uint32_t w) {
  dst4[0] = (uint8_t)(w & 0xFFu);
  dst4[1] = (uint8_t)((w >> 8) & 0xFFu);
  dst4[2] = (uint8_t)((w >> 16) & 0xFFu);
  dst4[3] = (uint8_t)((w >> 24) & 0xFFu);
}

static inline uint32_t read_le_u32(const uint8_t *src4) {
  return ((uint32_t)src4[0]) | ((uint32_t)src4[1] << 8) |
         ((uint32_t)src4[2] << 16) | ((uint32_t)src4[3] << 24);
}

static venus_status_t copy_words_to_buf(void *dst_buf, uint32_t dst_bytes,
                                        const uint32_t *src_words,
                                        size_t src_len_words) {
  if (!dst_buf || !src_words) {
    return VENUS_ERR_INVALID_ARG;
  }
  const uint32_t need = (uint32_t)(src_len_words * 4u);
  if (need > dst_bytes) {
    return VENUS_ERR_NO_MEM;
  }
  uint8_t *dst = (uint8_t *)dst_buf;
  for (size_t i = 0; i < src_len_words; ++i) {
    write_le_u32(&dst[i * 4u], src_words[i]);
  }
  return VENUS_OK;
}

static venus_status_t patch_uops_offset_mode(uint8_t *uops_buf,
                                             size_t uops_len_words,
                                             uintptr_t act_base_pa,
                                             uintptr_t param_base_pa) {
  if (!uops_buf) {
    return VENUS_ERR_INVALID_ARG;
  }
  if ((uops_len_words % VENUS_UOP_WORDS) != 0u) {
    return VENUS_ERR_BUNDLE_FORMAT;
  }
  if (sizeof(uintptr_t) > 4u &&
      (act_base_pa > 0xFFFFFFFFu || param_base_pa > 0xFFFFFFFFu)) {
    return VENUS_ERR_ADDR_TRUNC;
  }

  const uint32_t act32 = (uint32_t)act_base_pa;
  const uint32_t par32 = (uint32_t)param_base_pa;
  const size_t uop_cnt = uops_len_words / VENUS_UOP_WORDS;

  for (size_t i = 0; i < uop_cnt; ++i) {
    const size_t base_word = i * VENUS_UOP_WORDS;
    const size_t base_byte = base_word * 4u;

    const uint32_t w3 = read_le_u32(&uops_buf[base_byte + 3u * 4u]);
    const uint32_t w4 = read_le_u32(&uops_buf[base_byte + 4u * 4u]);
    const uint32_t w5 = read_le_u32(&uops_buf[base_byte + 5u * 4u]);

    write_le_u32(&uops_buf[base_byte + 3u * 4u], (uint32_t)(w3 + par32));
    write_le_u32(&uops_buf[base_byte + 4u * 4u], (uint32_t)(w4 + act32));
    write_le_u32(&uops_buf[base_byte + 5u * 4u], (uint32_t)(w5 + act32));
  }
  return VENUS_OK;
}

venus_status_t venus_patch_uops_offset_mode_inplace(void *uops_buf,
                                                    size_t uops_len_words,
                                                    uintptr_t act_base_pa,
                                                    uintptr_t param_base_pa) {
  return patch_uops_offset_mode((uint8_t *)uops_buf, uops_len_words, act_base_pa, param_base_pa);
}

// =============================================================================
// 6) One-shot run
// =============================================================================

venus_status_t venus_run_bundle(const venus_bundle_t *bundle,
                                const venus_mem_t *mem,
                                uint32_t timeout_cycles,
                                uint32_t *out_hw_status) {
  if (!bundle || !mem || !mem->uops_buf || !mem->params_buf) {
    return VENUS_ERR_INVALID_ARG;
  }
  if (bundle->uops_len_words == 0u || bundle->params_len_words == 0u) {
    return VENUS_ERR_BUNDLE_FORMAT;
  }
  if ((bundle->uops_len_words % VENUS_UOP_WORDS) != 0u) {
    return VENUS_ERR_BUNDLE_FORMAT;
  }

  venus_status_t st;
  st = copy_words_to_buf(mem->params_buf, mem->params_buf_bytes,
                         bundle->params_words, bundle->params_len_words);
  if (st != VENUS_OK) {
    return st;
  }
  venus_cache_flush(mem->params_buf,
                    (size_t)(bundle->params_len_words * 4u));

  st = copy_words_to_buf(mem->uops_buf, mem->uops_buf_bytes,
                         bundle->uops_words, bundle->uops_len_words);
  if (st != VENUS_OK) {
    return st;
  }

  if (bundle->address_mode_offset != 0u) {
    st = patch_uops_offset_mode((uint8_t *)mem->uops_buf,
                                bundle->uops_len_words, mem->act_base_pa,
                                mem->params_buf_pa);
    if (st != VENUS_OK) {
      return st;
    }
  }

  venus_cache_flush(mem->uops_buf,
                    (size_t)(bundle->uops_len_words * 4u));

  const uint32_t uop_count =
      (uint32_t)(bundle->uops_len_words / VENUS_UOP_WORDS);
  st = venus_submit_and_start(mem->uops_buf_pa, uop_count);
  if (st != VENUS_OK) {
    return st;
  }

  uint32_t hw_status = 0;
  st = venus_wait_idle(timeout_cycles, &hw_status);
  if (out_hw_status) {
    *out_hw_status = hw_status;
  }
  return st;
}

venus_status_t venus_run_bundle_irq(const venus_bundle_t *bundle,
                                    const venus_mem_t *mem,
                                    uint32_t timeout_cycles,
                                    uint32_t *out_hw_status) {
  if (!bundle || !mem || !mem->uops_buf || !mem->params_buf) {
    return VENUS_ERR_INVALID_ARG;
  }
  if (bundle->uops_len_words == 0u || bundle->params_len_words == 0u) {
    return VENUS_ERR_BUNDLE_FORMAT;
  }
  if ((bundle->uops_len_words % VENUS_UOP_WORDS) != 0u) {
    return VENUS_ERR_BUNDLE_FORMAT;
  }

  venus_status_t st;
  st = copy_words_to_buf(mem->params_buf, mem->params_buf_bytes,
                         bundle->params_words, bundle->params_len_words);
  if (st != VENUS_OK) {
    return st;
  }
  venus_cache_flush(mem->params_buf,
                    (size_t)(bundle->params_len_words * 4u));

  st = copy_words_to_buf(mem->uops_buf, mem->uops_buf_bytes,
                         bundle->uops_words, bundle->uops_len_words);
  if (st != VENUS_OK) {
    return st;
  }

  if (bundle->address_mode_offset != 0u) {
    st = patch_uops_offset_mode((uint8_t *)mem->uops_buf,
                                bundle->uops_len_words, mem->act_base_pa,
                                mem->params_buf_pa);
    if (st != VENUS_OK) {
      return st;
    }
  }

  venus_cache_flush(mem->uops_buf,
                    (size_t)(bundle->uops_len_words * 4u));

  // Enable NPU interrupts (done/error)
  venus_reg_write(VENUS_REG_INT_EN, (INT_DONE_MASK | INT_ERROR_MASK));

  const uint32_t uop_count =
      (uint32_t)(bundle->uops_len_words / VENUS_UOP_WORDS);
  st = venus_submit_and_start(mem->uops_buf_pa, uop_count);
  if (st != VENUS_OK) {
    return st;
  }

  uint32_t hw_status = 0;
  st = venus_wait_irq(timeout_cycles, &hw_status);
  if (out_hw_status) {
    *out_hw_status = hw_status;
  }
  return st;
}
