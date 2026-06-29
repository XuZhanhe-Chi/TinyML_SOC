/*
 * kws_frontend_fixed.h
 * -----------------------------------------------------------------------------
 * Fixed-point KWS frontend (log-mel + CMVN + pack) for kws_npu_bench.
 * All math is integer; no float dependencies.
 */

#ifndef KWS_FRONTEND_FIXED_H
#define KWS_FRONTEND_FIXED_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  KWS_SR_HZ = 16000,
  KWS_CLIP_SAMPLES = 16000,
  KWS_FRAME_LENGTH = 640, // 40ms
  KWS_FRAME_STEP = 320,   // 20ms
  KWS_N_FFT = 1024,
  KWS_N_MELS = 40,
  KWS_FEAT_T = 50,
  KWS_FEAT_F = 40,
};

typedef struct {
  // Q15 window and twiddle tables.
  int16_t win_1024[KWS_N_FFT];
  int16_t tw_cos[KWS_N_FFT / 2];
  int16_t tw_sin[KWS_N_FFT / 2];

  // Mel filter edges (fft bin indices), length 42 (n_mels+2).
  uint16_t mel_bins[KWS_N_MELS + 2];
} kws_frontend_t;

typedef struct {
  // Interleaved complex FFT buffer: [re0, im0, re1, im1, ...] (Q15).
  int16_t fft_cpx[2 * KWS_N_FFT];
  // Power spectrum buffer: [0..N/2] (Q30).
  uint32_t power[KWS_N_FFT / 2 + 1];
} kws_frontend_workbuf_t;

// Initialize tables (window, twiddles, mel bins). Returns 0 on success.
int kws_frontend_init(kws_frontend_t *fe);

// Compute one log-mel frame (Q8.8) from a ring buffer.
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
                                        int32_t out_logmel_q8[KWS_FEAT_F]);

// Apply CMVN mean (subtract per-bin mean in-place), input Q8.8.
void kws_cmvn_mean_frame(int32_t feat_q8[KWS_FEAT_F]);

// Quantize + pack to NCHWc4 int8 (C4=1) expected by VenusCore KWS bundle.
void kws_pack_frame_nchw_c4_i8(const int32_t feat_q8[KWS_FEAT_F],
                               int32_t input_scale_q16,
                               uint8_t *dst_frame);

#ifdef __cplusplus
}
#endif

#endif // KWS_FRONTEND_FIXED_H
