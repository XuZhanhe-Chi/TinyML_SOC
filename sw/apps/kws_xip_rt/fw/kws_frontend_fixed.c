/*
 * kws_frontend_fixed.c
 * -----------------------------------------------------------------------------
 * Fixed-point KWS frontend used by kws_npu_bench.
 * - Q15 window/twiddle, Q15 FFT, Q30 power
 * - log-mel output in Q8.8, CMVN in Q8.8
 * - No floating-point operations
 */

#include "kws_frontend_fixed.h"
#include "kws_frontend_fixed_tables.h"

#include <string.h>

#define KWS_LOG_MEL_MIN_Q30 1024u
#define KWS_LN2_Q8 177
#define KWS_LOG_OFFSET_Q8 3547

static inline int16_t sat_i16(int32_t x) {
  if (x > 32767) {
    return 32767;
  }
  if (x < -32768) {
    return -32768;
  }
  return (int16_t)x;
}

static inline int8_t sat_i8(int32_t x) {
  if (x > 127) {
    return 127;
  }
  if (x < -128) {
    return -128;
  }
  return (int8_t)x;
}

static inline int reflect_idx_i32(int idx, int len) {
  if (len <= 1) {
    return 0;
  }
  const int period = 2 * (len - 1);
  int x = idx;
  if (x < 0) {
    x = -x;
  }
  x %= period;
  if (x >= len) {
    x = period - x;
  }
  return x;
}

static inline int32_t log2_q8_u32(uint32_t x) {
  if (x == 0u) {
    return (int32_t)0x80000000u;
  }
  const int n = 31 - __builtin_clz(x);
  uint32_t mant;
  if (n >= 15) {
    mant = x >> (n - 15);
  } else {
    mant = x << (15 - n);
  }
  const uint32_t frac = (mant - (1u << 15)) >> 7; // top 8 bits
  return (n << 8) + (int32_t)KWS_LOG2_LUT_Q8[frac];
}

static inline int32_t ln_q8_from_q30(uint32_t x_q30) {
  uint32_t v = x_q30;
  if (v < KWS_LOG_MEL_MIN_Q30) {
    v = KWS_LOG_MEL_MIN_Q30;
  }
  int32_t log2_q8 = log2_q8_u32(v);
  log2_q8 -= (30 << 8); // Q30 to real
  int32_t ln_q8 = (log2_q8 * KWS_LN2_Q8) >> 8;
  // Compensate FFT stage scaling (1/2 per stage, 10 stages => 20 in power).
  ln_q8 += KWS_LOG_OFFSET_Q8;
  return ln_q8;
}

static void fft_inplace_1024(const kws_frontend_t *fe, int16_t *a) {
  // Bit-reversal permutation (N=1024 -> 10 bits).
  for (uint32_t i = 0; i < KWS_N_FFT; ++i) {
    uint32_t x = i;
    uint32_t r = 0;
    for (int b = 0; b < 10; ++b) {
      r = (r << 1) | (x & 1u);
      x >>= 1;
    }
    if (r > i) {
      const uint32_t i2 = i * 2u;
      const uint32_t r2 = r * 2u;
      const int16_t tr = a[i2];
      const int16_t ti = a[i2 + 1u];
      a[i2] = a[r2];
      a[i2 + 1u] = a[r2 + 1u];
      a[r2] = tr;
      a[r2 + 1u] = ti;
    }
  }

  for (uint32_t len = 2; len <= KWS_N_FFT; len <<= 1) {
    const uint32_t half = len >> 1;
    const uint32_t step = KWS_N_FFT / len;
    for (uint32_t i = 0; i < KWS_N_FFT; i += len) {
      for (uint32_t j = 0; j < half; ++j) {
        const uint32_t tw = j * step;
        const int16_t wr = fe->tw_cos[tw];
        const int16_t wi = fe->tw_sin[tw];

        const uint32_t a0 = (i + j) * 2u;
        const uint32_t a1 = (i + j + half) * 2u;
        const int32_t ur = a[a0];
        const int32_t ui = a[a0 + 1u];
        const int32_t vr = a[a1];
        const int32_t vi = a[a1 + 1u];

        const int32_t tr = (vr * wr - vi * wi) >> 15;
        const int32_t ti = (vr * wi + vi * wr) >> 15;

        const int32_t xr = (ur + tr) >> 1;
        const int32_t xi = (ui + ti) >> 1;
        const int32_t yr = (ur - tr) >> 1;
        const int32_t yi = (ui - ti) >> 1;

        a[a0] = sat_i16(xr);
        a[a0 + 1u] = sat_i16(xi);
        a[a1] = sat_i16(yr);
        a[a1 + 1u] = sat_i16(yi);
      }
    }
  }
}

int kws_frontend_init(kws_frontend_t *fe) {
  if (!fe) {
    return -1;
  }
  memcpy(fe->win_1024, KWS_WIN_1024_Q15, sizeof(KWS_WIN_1024_Q15));
  memcpy(fe->tw_cos, KWS_TW_COS_Q15, sizeof(KWS_TW_COS_Q15));
  memcpy(fe->tw_sin, KWS_TW_SIN_Q15, sizeof(KWS_TW_SIN_Q15));
  memcpy(fe->mel_bins, KWS_MEL_BINS, sizeof(KWS_MEL_BINS));
  return 0;
}

int kws_frontend_logmel_frame_ring_norm(const kws_frontend_t *fe,
                                        const int16_t *ring,
                                        uint32_t ring_mask,
                                        uint32_t sample_idx,
                                        int frame_idx,
                                        kws_frontend_workbuf_t *wb,
                                        int dc_remove,
                                        int agc_enable,
                                        int32_t agc_target_rms_q16,
                                        int32_t agc_max_gain_q16,
                                        int32_t out_logmel_q8[KWS_FEAT_F]) {
  if (!fe || !ring || !out_logmel_q8 || !wb) {
    return -1;
  }
  (void)sample_idx;
  (void)agc_enable;
  (void)agc_target_rms_q16;
  (void)agc_max_gain_q16;
  if (frame_idx < 0 || frame_idx >= KWS_FEAT_T) {
    return -1;
  }

  const int pad = KWS_N_FFT / 2;
  const int frame_start = frame_idx * KWS_FRAME_STEP - pad;
  const int pad_left = (KWS_N_FFT - KWS_FRAME_LENGTH) / 2;
  const int frame_base = frame_start + pad_left;

  int32_t mean = 0;
  if (dc_remove) {
    int64_t sum = 0;
    for (int i = 0; i < KWS_FRAME_LENGTH; ++i) {
      const int idx = frame_base + i;
      const int ridx = reflect_idx_i32(idx, KWS_CLIP_SAMPLES);
      sum += (int64_t)ring[((uint32_t)ridx) & ring_mask];
    }
    mean = (int32_t)(sum / (int64_t)KWS_FRAME_LENGTH);
  }

  int16_t *fft_buf = wb->fft_cpx;
  for (int n = 0; n < KWS_N_FFT; ++n) {
    const int idx = frame_start + n;
    const int ridx = reflect_idx_i32(idx, KWS_CLIP_SAMPLES);
    const uint32_t ring_idx = ((uint32_t)ridx) & ring_mask;
    int32_t v = (int32_t)ring[ring_idx] - mean;
    int32_t w = (v * (int32_t)fe->win_1024[n]) >> 15;
    fft_buf[n * 2] = sat_i16(w);
    fft_buf[n * 2 + 1] = 0;
  }

  fft_inplace_1024(fe, fft_buf);

  for (int k = 0; k < (KWS_N_FFT / 2 + 1); ++k) {
    const int32_t r = fft_buf[k * 2];
    const int32_t im = fft_buf[k * 2 + 1];
    wb->power[k] = (uint32_t)(r * r + im * im);
  }

  for (int m = 0; m < KWS_N_MELS; ++m) {
    int f_left = (int)fe->mel_bins[m + 0];
    int f_center = (int)fe->mel_bins[m + 1];
    int f_right = (int)fe->mel_bins[m + 2];

    if (f_center <= f_left) {
      f_center = f_left + 1;
    }
    if (f_right <= f_center) {
      f_right = f_center + 1;
    }
    if (f_right > (KWS_N_FFT / 2)) {
      f_right = (KWS_N_FFT / 2);
    }

    uint64_t mel_e = 0;

    const int inc_len = f_center - f_left;
    for (int f = f_left; f < f_center; ++f) {
      const int idx = f - f_left;
      const int32_t w = (inc_len > 0) ? ((idx << 15) / inc_len) : 0;
      mel_e += ((uint64_t)wb->power[f] * (uint32_t)w) >> 15;
    }

    const int dec_len = f_right - f_center;
    for (int f = f_center; f < f_right; ++f) {
      const int idx = f - f_center;
      const int32_t w = (dec_len > 0) ? (((dec_len - idx) << 15) / dec_len) : 0;
      mel_e += ((uint64_t)wb->power[f] * (uint32_t)w) >> 15;
    }

    uint32_t mel_q30 = (mel_e > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)mel_e;
    out_logmel_q8[m] = ln_q8_from_q30(mel_q30);
  }

  return 0;
}

void kws_cmvn_mean_frame(int32_t feat_q8[KWS_FEAT_F]) {
  if (!feat_q8) {
    return;
  }
  for (int f = 0; f < KWS_FEAT_F; ++f) {
    feat_q8[f] -= (int32_t)KWS_CMVM_MEAN_Q8[f];
  }
}

void kws_pack_frame_nchw_c4_i8(const int32_t feat_q8[KWS_FEAT_F],
                               int32_t input_scale_q16,
                               uint8_t *dst_frame) {
  if (!feat_q8 || !dst_frame) {
    return;
  }
  if (input_scale_q16 <= 0) {
    return;
  }

  for (int f = 0; f < KWS_FEAT_F; ++f) {
    int32_t num = feat_q8[f] << 8; // Q8.8 -> Q16.16
    int32_t q;
    if (num >= 0) {
      q = (num + (input_scale_q16 / 2)) / input_scale_q16;
    } else {
      q = (num - (input_scale_q16 / 2)) / input_scale_q16;
    }
    const int8_t v = sat_i8(q);
    dst_frame[f * 4 + 0] = (uint8_t)v;
    dst_frame[f * 4 + 1] = 0;
    dst_frame[f * 4 + 2] = 0;
    dst_frame[f * 4 + 3] = 0;
  }
}
