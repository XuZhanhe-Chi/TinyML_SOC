#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "core/soc.h"
#include "core/riscv.h"
#include "drivers/uart/uart.h"
#include "drivers/timer/murax_timer.h"
#include "drivers/venus/venus_driver.h"
#include "drivers/gpio/gpio.h"

#include "msm261s.h"
#include "kws_frontend_fixed.h"
#include "kws_testvector_fpga.h"
#include "kws_xip_rt.h"

typedef struct {
  uint32_t clip_id;
  uint32_t avg_abs;
  uint32_t peak_abs;
} kws_infer_msg_t;

static uart_t g_uart;
static gpio_t g_gpio;
static QueueHandle_t g_infer_queue;
static volatile uint32_t g_infer_busy = 0u;
static volatile uint32_t g_clip_seq = 0u;

static volatile uint32_t g_npu_elapsed_cycles = 0u;
static volatile uint32_t g_npu_start_cycle = 0u;

static const char *const g_kws_labels[KWS_NUM_CLASSES] = {
    "one",   "two",  "three", "four", "five",  "six",
    "seven", "eight", "up",    "down", "noise", "silence",
};

__attribute__((section(".venus_act"), aligned(64)))
static uint8_t g_act[VENUS_ACT_BUF_BYTES];

__attribute__((aligned(64)))
static uint8_t g_uops_buf[UOPS_LEN_BYTES];

static kws_frontend_t g_fe;
static kws_frontend_workbuf_t *g_fe_wb = NULL;

typedef struct {
  int16_t ring[KWS_N_FFT];
  uint32_t sample_count;
  uint32_t frame_idx;
  uint32_t sum_abs;
  uint32_t peak_abs;
} kws_stream_t;

typedef struct {
  int16_t samples[KWS_VAD_PREROLL_SAMPLES];
  uint32_t head;
  uint32_t count;
} kws_preroll_t;

typedef struct {
  int best_idx;
  int second_idx;
  int8_t best_score;
  int8_t second_score;
} kws_top2_t;

static kws_stream_t g_stream;
static kws_preroll_t g_preroll;
static int32_t g_frame_q8[KWS_FEAT_F];

static venus_bundle_t g_bundle;
static uint32_t g_uop_count = 0u;
static int32_t g_input_scale_q16 = (int32_t)KWS_INPUT_SCALE_Q16;

static StaticQueue_t g_infer_queue_struct;
static uint8_t g_infer_queue_storage[KWS_AUDIO_QUEUE_LEN *
                                     (uint32_t)sizeof(kws_infer_msg_t)];

static StaticTask_t g_task_audio_tcb;
static StaticTask_t g_task_infer_tcb;
static StackType_t g_task_audio_stack[KWS_TASK_STACK_AUDIO];
static StackType_t g_task_infer_stack[KWS_TASK_STACK_INFER];

static StaticTask_t g_task_idle_tcb;
static StackType_t g_task_idle_stack[configMINIMAL_STACK_SIZE];

static uint8_t g_gpio_last = 0xFFu;

#if (KWS_GPIO_STATUS_EN != 0)
static inline void kws_gpio_status(uint8_t state, uint8_t payload) {
#if (KWS_GPIO_LED_RESULT_MODE != 0)
  (void)state;
  (void)payload;
#else
  const uint8_t v = (uint8_t)(((state & 0xFu) << 4) | (payload & 0xFu));
  if (v != g_gpio_last) {
    g_gpio_last = v;
    gpio_write(g_gpio, (uint32_t)v);
  }
#endif
}
#else
static inline void kws_gpio_status(uint8_t state, uint8_t payload) {
  (void)state;
  (void)payload;
}
#endif

#if (KWS_GPIO_ACTIVITY_EN != 0)
#define KWS_GPIO_ACTIVITY(state, payload) kws_gpio_status((state), (payload))
#else
#define KWS_GPIO_ACTIVITY(state, payload) \
  do {                                   \
    (void)(state);                       \
    (void)(payload);                     \
  } while (0)
#endif

#if (KWS_UART_LOG_EN != 0)
#define KWS_LOG(...) __VA_ARGS__
#else
#define KWS_LOG(...) \
  do {              \
  } while (0)
#endif

static int argmax_i8(const int8_t *data, uint32_t count) {
  int best_idx = 0;
  int8_t best_val = data[0];
  for (uint32_t i = 1; i < count; ++i) {
    if (data[i] > best_val) {
      best_val = data[i];
      best_idx = (int)i;
    }
  }
  return best_idx;
}

static kws_top2_t top2_i8(const int8_t *data, uint32_t count) {
  kws_top2_t r = {
      .best_idx = 0,
      .second_idx = 0,
      .best_score = data[0],
      .second_score = INT8_MIN,
  };
  for (uint32_t i = 1; i < count; ++i) {
    const int8_t v = data[i];
    if (v > r.best_score) {
      r.second_score = r.best_score;
      r.second_idx = r.best_idx;
      r.best_score = v;
      r.best_idx = (int)i;
    } else if (v > r.second_score) {
      r.second_score = v;
      r.second_idx = (int)i;
    }
  }
  return r;
}

static uint32_t abs_i16_u32(int16_t x) {
  const int32_t v = (int32_t)x;
  return (uint32_t)((v < 0) ? -v : v);
}

static int kws_vad_active(uint32_t avg_abs, uint32_t peak_abs) {
#if (KWS_VAD_ENABLE != 0)
  return (avg_abs >= (uint32_t)KWS_VAD_AVG_ABS_TH) &&
         (peak_abs >= (uint32_t)KWS_VAD_PEAK_TH);
#else
  (void)avg_abs;
  (void)peak_abs;
  return 1;
#endif
}

static int kws_vad_trigger(uint32_t avg_abs, uint32_t peak_abs) {
#if (KWS_VAD_ENABLE != 0)
  return (avg_abs >= (uint32_t)KWS_VAD_TRIGGER_AVG_ABS_TH) &&
         (peak_abs >= (uint32_t)KWS_VAD_TRIGGER_PEAK_TH);
#else
  (void)avg_abs;
  (void)peak_abs;
  return 1;
#endif
}

static int kws_is_command_idx(int idx) {
  const uint32_t uidx = (uint32_t)idx;
  return (uidx < (uint32_t)KWS_NUM_CLASSES) &&
         (uidx != (uint32_t)KWS_NOISE_CLASS_IDX) &&
         (uidx != (uint32_t)KWS_SILENCE_CLASS_IDX);
}

static int kws_confident_command(const kws_top2_t *top) {
  const int32_t score = (int32_t)top->best_score;
  const int32_t margin = score - (int32_t)top->second_score;
  return kws_is_command_idx(top->best_idx) &&
         (score >= (int32_t)KWS_CONF_SCORE_TH) &&
         (margin >= (int32_t)KWS_CONF_MARGIN_TH);
}

#if (KWS_GPIO_LED_RESULT_MODE != 0)
static uint8_t kws_led_mask_for_idx(int idx) {
  uint8_t mask = 0u;
  if (idx >= 0 && idx <= 7) {
    mask = (uint8_t)(1u << (uint32_t)idx);
  } else if ((uint32_t)idx == 8u) {
    mask = 0xFFu; // up
  } else if ((uint32_t)idx == 9u) {
    mask = 0x00u; // down
  }
#if (KWS_LED_ACTIVE_LOW != 0)
  mask = (uint8_t)~mask;
#endif
  return mask;
}
#endif

static int kws_gpio_result(int idx, uint8_t *readback) {
#if (KWS_GPIO_STATUS_EN != 0)
#if (KWS_GPIO_LED_RESULT_MODE != 0)
  const uint8_t mask = kws_led_mask_for_idx(idx);
  if (mask != g_gpio_last) {
    g_gpio_last = mask;
    gpio_write(g_gpio, (uint32_t)mask);
  }
  (void)gpio_read(g_gpio);
  const uint8_t actual = (uint8_t)gpio_read(g_gpio);
  if (readback != NULL) {
    *readback = actual;
  }
  return actual == mask;
#else
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_DONE, (uint8_t)idx);
  if (readback != NULL) {
    *readback = 0u;
  }
  return 1;
#endif
#else
  (void)idx;
  if (readback != NULL) {
    *readback = 0u;
  }
  return 1;
#endif
}

static uint32_t align_up_u32(uint32_t v, uint32_t a) {
  if (a == 0u) {
    return v;
  }
  const uint32_t r = v % a;
  return (r == 0u) ? v : (v + (a - r));
}

static int32_t kws_float_bits_to_q16(uint32_t bits) {
  const uint32_t sign = bits >> 31;
  const uint32_t exp = (bits >> 23) & 0xFFu;
  const uint32_t mant = bits & 0x7FFFFFu;
  if (sign || exp == 0u || exp == 0xFFu) {
    return 0;
  }
  const int32_t e = (int32_t)exp - 127;
  const uint32_t sig = (1u << 23) | mant;
  const int32_t shift = e - 7; // Q16 scale
  uint64_t v = 0;
  if (shift >= 0) {
    v = ((uint64_t)sig) << shift;
  } else {
    const int32_t rshift = -shift;
    v = ((uint64_t)sig + (1ull << (rshift - 1))) >> rshift;
  }
  if (v > 0x7FFFFFFFu) {
    v = 0x7FFFFFFFu;
  }
  return (int32_t)v;
}

static int32_t kws_try_get_tensor_scale_q16(const vc_plan_t *plan,
                                            uint32_t offset_bytes,
                                            uint32_t size_bytes,
                                            int32_t fallback_q16) {
  if (!plan || !plan->tensors || !plan->quant_scales) {
    return fallback_q16;
  }
  for (uint32_t i = 0; i < plan->tensor_count; ++i) {
    const vc_tensor_desc_t *t = &plan->tensors[i];
    if (t->offset_bytes == offset_bytes && t->size_bytes == size_bytes) {
      if (t->quant_index == 0xFFFFu) {
        return fallback_q16;
      }
      if (t->quant_index >= plan->quant_scale_count) {
        return fallback_q16;
      }
      uint32_t bits = 0u;
      memcpy(&bits, &plan->quant_scales[t->quant_index], sizeof(bits));
      const int32_t q16 = kws_float_bits_to_q16(bits);
      return (q16 > 0) ? q16 : fallback_q16;
    }
  }
  return fallback_q16;
}

static void kws_stream_reset(kws_stream_t *st) {
  if (!st) {
    return;
  }
  st->sample_count = 0u;
  st->frame_idx = 0u;
  st->sum_abs = 0u;
  st->peak_abs = 0u;
}

static void kws_preroll_reset(kws_preroll_t *pr) {
  if (!pr) {
    return;
  }
  pr->head = 0u;
  pr->count = 0u;
}

static void kws_preroll_push(kws_preroll_t *pr, int16_t sample) {
  if (!pr) {
    return;
  }
  pr->samples[pr->head] = sample;
  pr->head++;
  if (pr->head >= (uint32_t)KWS_VAD_PREROLL_SAMPLES) {
    pr->head = 0u;
  }
  if (pr->count < (uint32_t)KWS_VAD_PREROLL_SAMPLES) {
    pr->count++;
  }
}

static int16_t kws_preroll_get_oldest(const kws_preroll_t *pr, uint32_t idx) {
  const uint32_t cap = (uint32_t)KWS_VAD_PREROLL_SAMPLES;
  const uint32_t start = (pr->head + cap - pr->count) % cap;
  return pr->samples[(start + idx) % cap];
}

static uint32_t kws_stream_required_max(uint32_t frame_idx) {
  const int pad = KWS_N_FFT / 2;
  const int frame_start = (int)frame_idx * (int)KWS_FRAME_STEP - pad;
  int raw_max = frame_start + (int)KWS_N_FFT - 1;
  int req_max = raw_max;
  if (frame_start < 0) {
    const int refl_max = -frame_start;
    if (refl_max > req_max) {
      req_max = refl_max;
    }
  }
  if (req_max >= (int)KWS_CLIP_SAMPLES) {
    req_max = (int)KWS_CLIP_SAMPLES - 1;
  }
  if (req_max < 0) {
    req_max = 0;
  }
  return (uint32_t)req_max;
}

static int kws_check_xip_layout(void) {
  const uintptr_t act_base = (uintptr_t)g_act;
  const uintptr_t param_base = (uintptr_t)params_words;

  if ((uint32_t)act_base != (uint32_t)KWS_ACT_BASE) {
    return -1;
  }
  // In offset address mode, params can be located anywhere in the QSPI window
  // (uOP relocation uses the runtime symbol address of params_words).
  // In absolute mode, users may still want a fixed KWS_PARAM_BASE.
  if (g_bundle.address_mode_offset == 0u) {
    if ((uint32_t)param_base != (uint32_t)KWS_PARAM_BASE) {
      return -1;
    }
  } else {
#ifndef KWS_QSPI_BASE
#define KWS_QSPI_BASE 0x00100000u
#endif
#ifndef KWS_QSPI_BYTES
#define KWS_QSPI_BYTES (4u * 1024u * 1024u)
#endif
    const uint32_t pb = (uint32_t)param_base;
    const uint32_t qspi_lo = (uint32_t)KWS_QSPI_BASE;
    const uint32_t qspi_hi = (uint32_t)(KWS_QSPI_BASE + KWS_QSPI_BYTES);
    if (pb < qspi_lo || pb >= qspi_hi) {
      return -1;
    }
  }
  return 0;
}

static inline uint32_t read_le_u32_u8(const uint8_t *p4) {
  return ((uint32_t)p4[0]) | ((uint32_t)p4[1] << 8) | ((uint32_t)p4[2] << 16) |
         ((uint32_t)p4[3] << 24);
}

static inline void write_le_u32_u8(uint8_t *p4, uint32_t w) {
  p4[0] = (uint8_t)(w & 0xFFu);
  p4[1] = (uint8_t)((w >> 8) & 0xFFu);
  p4[2] = (uint8_t)((w >> 16) & 0xFFu);
  p4[3] = (uint8_t)((w >> 24) & 0xFFu);
}

#if (KWS_UART_LOG_EN != 0)
static void uart_write_hex32_spin(uart_t uart, uint32_t v) {
  static const char kHex[16] = "0123456789ABCDEF";
  uart_write_spin(uart, "0x", KWS_UART_SPIN_MAX);
  for (int i = 7; i >= 0; --i) {
    const uint32_t nib = (v >> (uint32_t)(i * 4)) & 0xFu;
    (void)uart_putc_spin(uart, kHex[nib], KWS_UART_SPIN_MAX);
  }
}
#else
static void uart_write_hex32_spin(uart_t uart, uint32_t v) {
  (void)uart;
  (void)v;
}
#endif

static void kws_prepare_uops(void);

static int kws_stream_push_sample(int16_t sample, uint32_t ring_mask) {
  if (g_stream.sample_count >= KWS_CLIP_SAMPLES) {
    return 1;
  }

  const uint32_t abs_sample = abs_i16_u32(sample);
  const uint32_t write_idx = g_stream.sample_count & ring_mask;
  g_stream.ring[write_idx] = sample;
  g_stream.sum_abs += abs_sample;
  if (abs_sample > g_stream.peak_abs) {
    g_stream.peak_abs = abs_sample;
  }
  g_stream.sample_count++;

#if (KWS_HEARTBEAT_EN != 0)
  if ((g_stream.sample_count % 2048u) == 0u) {
    KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_STREAM,
                      (uint8_t)(g_stream.frame_idx & 0xFu));
  }
#endif

  const uint32_t sample_idx = g_stream.sample_count - 1u;
  while (g_stream.frame_idx < KWS_FEAT_T) {
    const uint32_t req_max = kws_stream_required_max(g_stream.frame_idx);
    if (sample_idx < req_max) {
      break;
    }
    if (kws_frontend_logmel_frame_ring_norm(&g_fe,
                                            g_stream.ring,
                                            ring_mask,
                                            sample_idx,
                                            (int)g_stream.frame_idx,
                                            g_fe_wb,
                                            KWS_AUDIO_DC_REMOVE,
                                            KWS_AUDIO_ENABLE_AGC,
                                            KWS_AGC_TARGET_RMS_Q16,
                                            KWS_AGC_MAX_GAIN_Q16,
                                            g_frame_q8) != 0) {
      return -1;
    }
    kws_cmvn_mean_frame(g_frame_q8);
    uint8_t *dst_frame = g_act + (uint32_t)g_bundle.input_base +
                         g_stream.frame_idx * (uint32_t)(KWS_FEAT_F * 4u);
    kws_pack_frame_nchw_c4_i8(g_frame_q8, g_input_scale_q16, dst_frame);
    g_stream.frame_idx++;
#if (KWS_HEARTBEAT_EN != 0)
    if ((g_stream.frame_idx % 10u) == 0u) {
      KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_STREAM,
                        (uint8_t)(g_stream.frame_idx & 0xFu));
    }
#endif
  }

  return ((g_stream.sample_count >= KWS_CLIP_SAMPLES) &&
          (g_stream.frame_idx >= KWS_FEAT_T))
             ? 1
             : 0;
}

static void kws_finalize_clip(uint32_t *reset_pending, uint32_t *capture_active) {
  const uint32_t clip_id = g_clip_seq++;
  const uint32_t avg_abs = g_stream.sum_abs / KWS_CLIP_SAMPLES;
  const uint32_t peak_abs = g_stream.peak_abs;
  if (!kws_vad_active(avg_abs, peak_abs)) {
#if (KWS_LOG_REJECTED_EVERY != 0)
    if ((clip_id % (uint32_t)KWS_LOG_REJECTED_EVERY) == 0u) {
      KWS_LOG(uart_write_spin(g_uart, "[KWS] vad_skip clip=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, clip_id, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " avg_abs=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, avg_abs, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " peak=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, peak_abs, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
    }
#endif
    kws_stream_reset(&g_stream);
    *capture_active = 0u;
    return;
  }

  KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_INFER, 0x0u);
  KWS_LOG(uart_write_spin(g_uart, "[KWS] audio_ready clip=", KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_u32_dec_spin(g_uart, clip_id, KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_spin(g_uart, " avg_abs=", KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_u32_dec_spin(g_uart, avg_abs, KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_spin(g_uart, " peak=", KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_u32_dec_spin(g_uart, peak_abs, KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
  kws_infer_msg_t msg = {
      .clip_id = clip_id,
      .avg_abs = avg_abs,
      .peak_abs = peak_abs,
  };
  g_infer_busy = 1u;
  if (xQueueSend(g_infer_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x8u);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] drop_clip\n", KWS_UART_SPIN_MAX););
    g_infer_busy = 0u;
  }
  *reset_pending = 1u;
  *capture_active = 0u;
}

static void kws_load_testvector_input(void) {
  uint8_t *dst = g_act + (uint32_t)g_bundle.input_base;
  for (uint32_t i = 0; i < (uint32_t)VC_KWS_INPUT_WORDS; ++i) {
    write_le_u32_u8(&dst[i * 4u], VC_KWS_INPUT_WORDS_U8X4_LE[i]);
  }
}

static int kws_run_testvector_once(void) {
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_TV, 0x0u);
  KWS_LOG(uart_write_spin(g_uart, "[KWS][TV] start\n", KWS_UART_SPIN_MAX););

  if ((uint32_t)g_bundle.input_size != (uint32_t)VC_KWS_INPUT_BYTES ||
      (uint32_t)g_bundle.input_base != (uint32_t)INPUT_BASE) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x1u);
    KWS_LOG(uart_write_spin(g_uart, "[KWS][TV] input mismatch bundle vs header\n", KWS_UART_SPIN_MAX););
    return -1;
  }
  if ((uint32_t)g_bundle.output_size != (uint32_t)KWS_NUM_CLASSES ||
      (uint32_t)g_bundle.output_base != (uint32_t)OUTPUT_BASE) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x2u);
    KWS_LOG(uart_write_spin(g_uart, "[KWS][TV] output mismatch bundle vs firmware\n", KWS_UART_SPIN_MAX););
    return -1;
  }

  murax_timer_init_tick_b(murax_timer_from_base(SOC_TIMER_BASE),
                          (uint16_t)KWS_TIMER_PRESCALER_LIMIT);
  venus_init();
  *((volatile uint32_t *)(uintptr_t)(VENUS_REG_BASE + VENUS_REG_INT_EN)) =
      (INT_DONE_MASK | INT_ERROR_MASK);

  kws_load_testvector_input();

  uint8_t *input_ptr = g_act + (uint32_t)g_bundle.input_base;
  venus_cache_flush(input_ptr, (size_t)g_bundle.input_size);

  uintptr_t uop_base = (uintptr_t)g_bundle.uops_words;
  if (g_bundle.address_mode_offset != 0u) {
    kws_prepare_uops();
    uop_base = (uintptr_t)g_uops_buf;
  }

  uint32_t hw_status = 0u;
  venus_status_t st = venus_submit_and_start(uop_base, g_uop_count);
  if (st == VENUS_OK) {
    st = venus_wait_irq(KWS_TIMEOUT_CYCLES, &hw_status);
  }
  if (st != VENUS_OK) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x3u);
    KWS_LOG(uart_write_spin(g_uart, "[KWS][TV] npu_failed=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, venus_strerror(st), KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, " status=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_u32_dec_spin(g_uart, hw_status, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
    return -1;
  }

  uint8_t *out_ptr = g_act + (uint32_t)g_bundle.output_base;
  venus_cache_invalidate(out_ptr, (size_t)g_bundle.output_size);
  const int8_t *out = (const int8_t *)out_ptr;
  const int best_idx = argmax_i8(out, KWS_NUM_CLASSES);
  const int8_t best_score = out[best_idx];

  KWS_LOG(uart_write_spin(g_uart, "[KWS][TV] top1_idx=", KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_u32_dec_spin(g_uart, (uint32_t)best_idx, KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_spin(g_uart, " expected=", KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_u32_dec_spin(g_uart, (uint32_t)VC_KWS_EXPECTED_TOP1_RTL, KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_spin(g_uart, " score=", KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_i32_dec_spin(g_uart, (int32_t)best_score, KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_spin(g_uart, " label=", KWS_UART_SPIN_MAX););
  if ((uint32_t)best_idx < KWS_NUM_CLASSES) {
    KWS_LOG(uart_write_spin(g_uart, g_kws_labels[best_idx], KWS_UART_SPIN_MAX););
  } else {
    KWS_LOG(uart_write_spin(g_uart, "unknown", KWS_UART_SPIN_MAX););
  }
  KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););

  if ((uint32_t)best_idx != (uint32_t)VC_KWS_EXPECTED_TOP1_RTL) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x4u);
    KWS_LOG(uart_write_spin(g_uart, "[KWS][TV] FAIL\n", KWS_UART_SPIN_MAX););
    return -1;
  }
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_TV, 0x1u);
  KWS_LOG(uart_write_spin(g_uart, "[KWS][TV] PASS\n", KWS_UART_SPIN_MAX););
  return 0;
}

static void kws_prepare_uops(void) {
  const uint32_t uops_bytes = (uint32_t)(g_bundle.uops_len_words * 4u);
  if (uops_bytes > (uint32_t)sizeof(g_uops_buf)) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x5u);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] uops_buf too small\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }

  const uint8_t *src = (const uint8_t *)(uintptr_t)g_bundle.uops_words;
  for (uint32_t i = 0; i < uops_bytes; ++i) {
    g_uops_buf[i] = src[i];
  }

  if (g_bundle.address_mode_offset != 0u) {
    const uint32_t act_base = (uint32_t)(uintptr_t)g_act;
    const uint32_t par_base = (uint32_t)(uintptr_t)params_words;
    const uint32_t uop_cnt =
        (uint32_t)(g_bundle.uops_len_words / (uint32_t)VENUS_UOP_WORDS);
    for (uint32_t i = 0; i < uop_cnt; ++i) {
      const uint32_t base = i * 32u;
      const uint32_t w3 = read_le_u32_u8(&g_uops_buf[base + 12u]);
      const uint32_t w4 = read_le_u32_u8(&g_uops_buf[base + 16u]);
      const uint32_t w5 = read_le_u32_u8(&g_uops_buf[base + 20u]);
      write_le_u32_u8(&g_uops_buf[base + 12u], w3 + par_base);
      write_le_u32_u8(&g_uops_buf[base + 16u], w4 + act_base);
      write_le_u32_u8(&g_uops_buf[base + 20u], w5 + act_base);
    }
  }

  venus_cache_flush(g_uops_buf, (size_t)uops_bytes);
}

__attribute__((section(".data")))
int venus_irq_wait(uint32_t timeout_cycles) {
  for (uint32_t i = 0; i < timeout_cycles; ++i) {
    const uint32_t int_status =
        *((volatile uint32_t *)(uintptr_t)(VENUS_REG_BASE + VENUS_REG_INT_STATUS));
    if (int_status & (INT_DONE_MASK | INT_ERROR_MASK)) {
      g_npu_elapsed_cycles = riscv_csr_read_mcycle() - g_npu_start_cycle;
      *((volatile uint32_t *)(uintptr_t)(VENUS_REG_BASE + VENUS_REG_INT_STATUS)) =
          (INT_DONE_MASK | INT_ERROR_MASK);
      return 1;
    }
    __asm__ volatile("nop");
  }
  return 0;
}

void venus_irq_mark_start(void) {
  g_npu_elapsed_cycles = 0u;
  g_npu_start_cycle = riscv_csr_read_mcycle();
}

static void task_audio(void *arg) {
  (void)arg;
  portENABLE_INTERRUPTS();
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_STREAM, 0x0u);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] task_audio start\n", KWS_UART_SPIN_MAX););

  MSM261S_Handle_t mic;
  const uintptr_t mic_base = (uintptr_t)SOC_I2S_MIC_BASE;
  if (mic_base == 0u) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x6u);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] mic base is 0\n", KWS_UART_SPIN_MAX););
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  uint32_t real_sr = MSM261S_Init(&mic, mic_base, KWS_MSM261S_PCLK_HZ, KWS_SR_HZ);
#ifdef KWS_I2S_DIV_OVERRIDE
  mic.Instance->DIV = (uint32_t)KWS_I2S_DIV_OVERRIDE;
  real_sr = (uint32_t)(KWS_MSM261S_PCLK_HZ /
                       (2u * ((uint32_t)KWS_I2S_DIV_OVERRIDE + 1u) * 64u));
#endif
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] ready mic_sr=", KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_u32_dec_spin(g_uart, real_sr, KWS_UART_SPIN_MAX););
  KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););

  int16_t tmp_buf[256];
  uint32_t ovf_cnt = 0u;
  uint32_t reset_pending = 0u;
  uint32_t capture_active = 0u;
  uint32_t listen_count = 0u;
  uint32_t listen_sum_abs = 0u;
  uint32_t listen_peak_abs = 0u;
  const uint32_t ring_mask = KWS_N_FFT - 1u;
#if (KWS_HEARTBEAT_EN != 0)
  TickType_t hb_last_tick = xTaskGetTickCount();
#endif

  kws_stream_reset(&g_stream);
  kws_preroll_reset(&g_preroll);

  for (;;) {
#if (KWS_HEARTBEAT_EN != 0)
    {
      const TickType_t now = xTaskGetTickCount();
      if ((now - hb_last_tick) >= pdMS_TO_TICKS((TickType_t)KWS_HEARTBEAT_MS)) {
        hb_last_tick = now;
        KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_STREAM,
                          (uint8_t)(g_stream.frame_idx & 0xFu));
      }
    }
#endif
    if (g_infer_busy) {
      if (MSM261S_HasData(&mic)) {
        (void)MSM261S_Read(&mic, tmp_buf, (uint32_t)(sizeof(tmp_buf) / sizeof(tmp_buf[0])));
      } else {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      reset_pending = 1u;
      capture_active = 0u;
    } else if (reset_pending) {
      kws_stream_reset(&g_stream);
      kws_preroll_reset(&g_preroll);
      capture_active = 0u;
      listen_count = 0u;
      listen_sum_abs = 0u;
      listen_peak_abs = 0u;
      reset_pending = 0u;
    } else {
      if (MSM261S_HasData(&mic)) {
        uint32_t got = MSM261S_Read(&mic, tmp_buf, (uint32_t)(sizeof(tmp_buf) / sizeof(tmp_buf[0])));
        for (uint32_t i = 0; i < got; ++i) {
          const uint32_t abs_sample = abs_i16_u32(tmp_buf[i]);
          if (!capture_active) {
            kws_preroll_push(&g_preroll, tmp_buf[i]);
            listen_count++;
            listen_sum_abs += abs_sample;
            if (abs_sample > listen_peak_abs) {
              listen_peak_abs = abs_sample;
            }
            if (listen_count >= (uint32_t)KWS_VAD_TRIGGER_SAMPLES) {
              const uint32_t listen_avg = listen_sum_abs / (uint32_t)KWS_VAD_TRIGGER_SAMPLES;
              if (kws_vad_trigger(listen_avg, listen_peak_abs)) {
                capture_active = 1u;
                kws_stream_reset(&g_stream);
                KWS_LOG(uart_write_spin(g_uart, "[KWS] vad_trigger avg_abs=", KWS_UART_SPIN_MAX););
                KWS_LOG(uart_write_u32_dec_spin(g_uart, listen_avg, KWS_UART_SPIN_MAX););
                KWS_LOG(uart_write_spin(g_uart, " peak=", KWS_UART_SPIN_MAX););
                KWS_LOG(uart_write_u32_dec_spin(g_uart, listen_peak_abs, KWS_UART_SPIN_MAX););
                KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
                for (uint32_t j = 0; j < g_preroll.count; ++j) {
                  const int push_ret =
                      kws_stream_push_sample(kws_preroll_get_oldest(&g_preroll, j), ring_mask);
                  if (push_ret < 0) {
                    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x7u);
                    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] frontend frame failed\n", KWS_UART_SPIN_MAX););
                    capture_active = 0u;
                    break;
                  }
                  if (push_ret > 0) {
                    kws_finalize_clip(&reset_pending, &capture_active);
                    break;
                  }
                }
              }
              listen_count = 0u;
              listen_sum_abs = 0u;
              listen_peak_abs = 0u;
            }
            continue;
          }

          const int push_ret = kws_stream_push_sample(tmp_buf[i], ring_mask);
          if (push_ret < 0) {
            kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x7u);
            KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] frontend frame failed\n", KWS_UART_SPIN_MAX););
            capture_active = 0u;
            break;
          }
          if (push_ret > 0) {
            kws_finalize_clip(&reset_pending, &capture_active);
            break;
          }
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }

    if (MSM261S_CheckAndClearOverflow(&mic)) {
      ovf_cnt++;
#if (KWS_LOG_MIC_OVERFLOW_EVERY != 0)
      if ((ovf_cnt % (uint32_t)KWS_LOG_MIC_OVERFLOW_EVERY) == 0u) {
        KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_STREAM, (uint8_t)(ovf_cnt & 0xFu));
        KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] mic_overflow=", KWS_UART_SPIN_MAX););
        KWS_LOG(uart_write_u32_dec_spin(g_uart, ovf_cnt, KWS_UART_SPIN_MAX););
        KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
      }
#endif
    }
  }
}

static void task_infer(void *arg) {
  (void)arg;
  portENABLE_INTERRUPTS();
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_INFER, 0x0u);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] task_infer start\n", KWS_UART_SPIN_MAX););

  venus_init();
  murax_timer_init_tick_b(murax_timer_from_base(SOC_TIMER_BASE),
                          (uint16_t)KWS_TIMER_PRESCALER_LIMIT);
  *((volatile uint32_t *)(uintptr_t)(VENUS_REG_BASE + VENUS_REG_INT_EN)) =
      (INT_DONE_MASK | INT_ERROR_MASK);
#if (KWS_HEARTBEAT_EN != 0)
  TickType_t hb_last_tick = xTaskGetTickCount();
#endif
  for (;;) {
    kws_infer_msg_t msg;
#if (KWS_HEARTBEAT_EN != 0)
    if (xQueueReceive(g_infer_queue, &msg,
                      pdMS_TO_TICKS((TickType_t)KWS_HEARTBEAT_MS)) != pdTRUE) {
      const TickType_t now = xTaskGetTickCount();
      if ((now - hb_last_tick) >= pdMS_TO_TICKS((TickType_t)KWS_HEARTBEAT_MS)) {
        hb_last_tick = now;
        KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_INFER, 0x0u);
      }
      continue;
    }
#else
    if (xQueueReceive(g_infer_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }
#endif
    KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_INFER, (uint8_t)(msg.clip_id & 0xFu));
    KWS_LOG(uart_write_spin(g_uart, "[KWS] infer_recv clip=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_u32_dec_spin(g_uart, msg.clip_id, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););

    uint8_t *input_ptr = g_act + (uint32_t)g_bundle.input_base;
    venus_cache_flush(input_ptr, (size_t)g_bundle.input_size);

    uint32_t hw_status = 0u;
    KWS_GPIO_ACTIVITY((uint8_t)KWS_GPIO_STATE_INFER, 0x1u);
    KWS_LOG(uart_write_spin(g_uart, "[KWS] npu_start clip=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_u32_dec_spin(g_uart, msg.clip_id, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
    uintptr_t uop_base = (uintptr_t)g_bundle.uops_words;
    if (g_bundle.address_mode_offset != 0u) {
      kws_prepare_uops();
      uop_base = (uintptr_t)g_uops_buf;
    }
    venus_status_t st = venus_submit_and_start(uop_base, g_uop_count);
    if (st == VENUS_OK) {
      st = venus_wait_irq(KWS_TIMEOUT_CYCLES, &hw_status);
    }
    if (st != VENUS_OK) {
      kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0x9u);
      KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] npu_failed=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, venus_strerror(st), KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " status=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, hw_status, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
      g_infer_busy = 0u;
      continue;
    }

    uint8_t *out_ptr = g_act + (uint32_t)g_bundle.output_base;
    venus_cache_invalidate(out_ptr, (size_t)g_bundle.output_size);
    const int8_t *out = (const int8_t *)out_ptr;
    const kws_top2_t top = top2_i8(out, KWS_NUM_CLASSES);
    const int32_t margin = (int32_t)top.best_score - (int32_t)top.second_score;
    const uint32_t elapsed_cycles = g_npu_elapsed_cycles;
    uint32_t elapsed_us = 0u;
    if (KWS_CORE_CLK_HZ != 0u) {
      elapsed_us = elapsed_cycles / (KWS_CORE_CLK_HZ / 1000000u);
    }

#if (KWS_LOG_RAW_TOP1 != 0)
    KWS_LOG(uart_write_spin(g_uart, "[KWS] raw_top1_idx=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_u32_dec_spin(g_uart, (uint32_t)top.best_idx, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, " score=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_i32_dec_spin(g_uart, (int32_t)top.best_score, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, " second=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_u32_dec_spin(g_uart, (uint32_t)top.second_idx, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, " margin=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_i32_dec_spin(g_uart, margin, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, " label=", KWS_UART_SPIN_MAX););
    if ((uint32_t)top.best_idx < KWS_NUM_CLASSES) {
      KWS_LOG(uart_write_spin(g_uart, g_kws_labels[top.best_idx], KWS_UART_SPIN_MAX););
    } else {
      KWS_LOG(uart_write_spin(g_uart, "unknown", KWS_UART_SPIN_MAX););
    }
    KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
#endif

    if (kws_confident_command(&top)) {
      uint8_t led_readback = 0u;
      if (!kws_gpio_result(top.best_idx, &led_readback)) {
        KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] gpio_readback expected=", KWS_UART_SPIN_MAX););
        KWS_LOG(uart_write_hex32_spin(g_uart, (uint32_t)g_gpio_last););
        KWS_LOG(uart_write_spin(g_uart, " actual=", KWS_UART_SPIN_MAX););
        KWS_LOG(uart_write_hex32_spin(g_uart, (uint32_t)led_readback););
        KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
        g_infer_busy = 0u;
        continue;
      }
      KWS_LOG(uart_write_spin(g_uart, "[KWS] detect idx=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, (uint32_t)top.best_idx, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " label=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, g_kws_labels[top.best_idx], KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " score=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_i32_dec_spin(g_uart, (int32_t)top.best_score, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " margin=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_i32_dec_spin(g_uart, margin, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " cycles=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, elapsed_cycles, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " us=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, elapsed_us, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " clip=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, msg.clip_id, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " avg_abs=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, msg.avg_abs, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " peak=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, msg.peak_abs, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " led=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_hex32_spin(g_uart, (uint32_t)led_readback););
      KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
    }
#if (KWS_LOG_REJECTED_EVERY != 0)
    else if ((msg.clip_id % (uint32_t)KWS_LOG_REJECTED_EVERY) == 0u) {
      KWS_LOG(uart_write_spin(g_uart, "[KWS] reject idx=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_u32_dec_spin(g_uart, (uint32_t)top.best_idx, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " score=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_i32_dec_spin(g_uart, (int32_t)top.best_score, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " margin=", KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_i32_dec_spin(g_uart, margin, KWS_UART_SPIN_MAX););
      KWS_LOG(uart_write_spin(g_uart, " label=", KWS_UART_SPIN_MAX););
      if ((uint32_t)top.best_idx < KWS_NUM_CLASSES) {
        KWS_LOG(uart_write_spin(g_uart, g_kws_labels[top.best_idx], KWS_UART_SPIN_MAX););
      } else {
        KWS_LOG(uart_write_spin(g_uart, "unknown", KWS_UART_SPIN_MAX););
      }
      KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
    }
#endif

    (void)elapsed_cycles;
    (void)elapsed_us;

    g_infer_busy = 0u;
  }
}

void vAssertCalled(const char *file, int line) {
  (void)file;
  (void)line;
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xFu);
  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

void vApplicationIdleHook(void) { portENABLE_INTERRUPTS(); }

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
  if (ppxIdleTaskTCBBuffer) {
    *ppxIdleTaskTCBBuffer = &g_task_idle_tcb;
  }
  if (ppxIdleTaskStackBuffer) {
    *ppxIdleTaskStackBuffer = g_task_idle_stack;
  }
  if (pulIdleTaskStackSize) {
    *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
  }
}

int main(void) {
  g_gpio = gpio_from_base(SOC_GPIO_BASE);
  gpio_set_output_enable(g_gpio, (uint32_t)KWS_STATUS_GPIO_MASK);
#if (KWS_GPIO_STATUS_EN != 0)
  // 在 XIP 仿真/特殊启动路径下，.data 复制异常会导致 g_gpio_last 初值不可信，
  // 从而让首次状态写入被“去重”跳过；这里强制复位一次，确保 TB 能看到 BOOT 状态。
  g_gpio_last = 0xFFu;
#endif
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_BOOT, 0x0u);

#if (KWS_UART_LOG_EN != 0)
  g_uart = uart_from_base(SOC_UART_BASE);
  uart_init(g_uart, KWS_UART_CLKDIV, 7u);
  uart_write_spin(g_uart, "[TinyML_SOC] boot\n", KWS_UART_SPIN_MAX);
#endif

  g_bundle = VENUS_BUNDLE_FROM_BUNDLE_H();
  g_uop_count = (uint32_t)(g_bundle.uops_len_words / (uint32_t)VENUS_UOP_WORDS);

#if defined(VC_HAS_PLAN) && (VC_HAS_PLAN != 0u)
  g_input_scale_q16 = kws_try_get_tensor_scale_q16(
      &VC_PLAN, g_bundle.input_base, g_bundle.input_size, g_input_scale_q16);
#endif

  if (kws_check_xip_layout() != 0) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xAu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] layout mismatch (act/param base)\n", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] act_base=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_hex32_spin(g_uart, (uint32_t)(uintptr_t)g_act););
    KWS_LOG(uart_write_spin(g_uart, " expected=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_hex32_spin(g_uart, (uint32_t)KWS_ACT_BASE););
    KWS_LOG(uart_write_spin(g_uart, "\n", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] param_base=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_hex32_spin(g_uart, (uint32_t)(uintptr_t)params_words););
    KWS_LOG(uart_write_spin(g_uart, " expected=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_hex32_spin(g_uart, (uint32_t)KWS_PARAM_BASE););
    KWS_LOG(uart_write_spin(g_uart, " (mode=", KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_u32_dec_spin(g_uart, (uint32_t)g_bundle.address_mode_offset, KWS_UART_SPIN_MAX););
    KWS_LOG(uart_write_spin(g_uart, ")\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }

  if (((uint32_t)INPUT_BASE + (uint32_t)INPUT_SIZE) > (uint32_t)VENUS_ACT_BUF_BYTES ||
      ((uint32_t)OUTPUT_BASE + (uint32_t)OUTPUT_SIZE) > (uint32_t)VENUS_ACT_BUF_BYTES) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xBu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] act buffer too small for bundle\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }

  if (KWS_RUN_TESTVECTOR != 0) {
    if (kws_run_testvector_once() != 0) {
      kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xCu);
      KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] testvector failed, stop\n", KWS_UART_SPIN_MAX););
      while (1) {
      }
    }
  }

#if (KWS_FAST_RT != 0)
  g_infer_queue = xQueueCreateStatic((UBaseType_t)KWS_AUDIO_QUEUE_LEN,
                                     (UBaseType_t)sizeof(kws_infer_msg_t),
                                     g_infer_queue_storage, &g_infer_queue_struct);
  if (g_infer_queue == NULL) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xDu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] queue init failed\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }

  TaskHandle_t h_infer =
      xTaskCreateStatic(task_infer, "infer", (uint32_t)KWS_TASK_STACK_INFER, NULL, 2,
                        g_task_infer_stack, &g_task_infer_tcb);
  if (h_infer == NULL) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xEu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] task create failed\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }

  // Trigger one inference using the existing input buffer (testvector already loaded it).
  kws_infer_msg_t msg = {.clip_id = 0u};
  (void)xQueueSend(g_infer_queue, &msg, 0);

  kws_gpio_status((uint8_t)KWS_GPIO_STATE_INFER, 0x0u);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] scheduler start\n", KWS_UART_SPIN_MAX););
  vTaskStartScheduler();

  kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xFu);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] scheduler failed\n", KWS_UART_SPIN_MAX););
  while (1) {
  }
#else

  kws_gpio_status((uint8_t)KWS_GPIO_STATE_TV, 0x2u);
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_FE_INIT, 0x0u);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] fe_init_start\n", KWS_UART_SPIN_MAX););
  if (kws_frontend_init(&g_fe) != 0) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xDu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] frontend init failed\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_FE_INIT, 0x1u);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] fe_init_ok\n", KWS_UART_SPIN_MAX););

  const uint32_t fe_wb_bytes = (uint32_t)sizeof(kws_frontend_workbuf_t);
  const uint32_t input_end = (uint32_t)g_bundle.input_base + (uint32_t)g_bundle.input_size;
  const uint32_t output_end = (uint32_t)g_bundle.output_base + (uint32_t)g_bundle.output_size;
  uint32_t fe_wb_off = align_up_u32(output_end, 64u);
  if (fe_wb_off < input_end || (fe_wb_off + fe_wb_bytes) > (uint32_t)VENUS_ACT_BUF_BYTES) {
    fe_wb_off = align_up_u32(input_end, 64u);
  }
  g_fe_wb = (kws_frontend_workbuf_t *)(void *)(g_act + fe_wb_off);
  if ((((uintptr_t)g_fe_wb) & 3u) != 0u) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xEu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] fe workbuf align error\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }
  if ((fe_wb_off + (uint32_t)sizeof(*g_fe_wb)) > (uint32_t)VENUS_ACT_BUF_BYTES) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xFu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] fe workbuf OOB\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_FE_INIT, 0x2u);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] fe_wb_ok\n", KWS_UART_SPIN_MAX););

  g_infer_queue = xQueueCreateStatic((UBaseType_t)KWS_AUDIO_QUEUE_LEN,
                                     (UBaseType_t)sizeof(kws_infer_msg_t),
                                     g_infer_queue_storage, &g_infer_queue_struct);
  if (g_infer_queue == NULL) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xFu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] queue init failed\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] queue ok\n", KWS_UART_SPIN_MAX););

  TaskHandle_t h_audio =
      xTaskCreateStatic(task_audio, "audio", (uint32_t)KWS_TASK_STACK_AUDIO, NULL,
                        3, g_task_audio_stack, &g_task_audio_tcb);
  TaskHandle_t h_infer =
      xTaskCreateStatic(task_infer, "infer", (uint32_t)KWS_TASK_STACK_INFER, NULL,
                        2, g_task_infer_stack, &g_task_infer_tcb);
  if (h_audio == NULL || h_infer == NULL) {
    kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xFu);
    KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] task create failed\n", KWS_UART_SPIN_MAX););
    while (1) {
    }
  }
  kws_gpio_status((uint8_t)KWS_GPIO_STATE_STREAM, 0x0u);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC] scheduler start\n", KWS_UART_SPIN_MAX););

  vTaskStartScheduler();

  kws_gpio_status((uint8_t)KWS_GPIO_STATE_FAIL, 0xFu);
  KWS_LOG(uart_write_spin(g_uart, "[TinyML_SOC][ERR] scheduler failed\n", KWS_UART_SPIN_MAX););
  while (1) {
  }
#endif
}

extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __data_load_start;

__attribute__((used)) static void boot_copy_data(void) {
  uint32_t *dst = &__data_start;
  uint32_t *src = &__data_load_start;
  while (dst < &__data_end) {
    *dst++ = *src++;
  }
}

__attribute__((used)) static void boot_clear_bss(void) {
  uint32_t *ptr = &__bss_start;
  while (ptr < &__bss_end) {
    *ptr++ = 0u;
  }
}

__attribute__((naked, used, section(".text._start"))) void _start(void) {
  __asm__ volatile(
      "li   sp, %[stack]\n\t"
      ".option push\n\t"
      ".option norelax\n\t"
      "la   gp, __global_pointer$\n\t"
      ".option pop\n\t"
      "jal  ra, boot_copy_data\n\t"
      "jal  ra, boot_clear_bss\n\t"
      "jal  ra, main\n\t"
      "1: j  1b\n\t"
      :
      : [stack] "i"(KWS_SRAM_BYTES - 0x100u)
      : "ra", "memory");
}
