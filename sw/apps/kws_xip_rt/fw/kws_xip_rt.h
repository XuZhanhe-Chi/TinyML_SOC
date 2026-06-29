#ifndef KWS_XIP_RT_H
#define KWS_XIP_RT_H

#include "bundle.h"

// 支持两种 address mode：
// - ADDRESS_MODE_OFFSET==1：uOP 中的 PARAM/FI/FO 为偏移，需要固件在运行前重定位到实际物理地址
// - ADDRESS_MODE_OFFSET==0：uOP 中为绝对物理地址（要求 bundle 与 linker 映射完全一致）

#ifndef KWS_UART_CLKDIV
#define KWS_UART_CLKDIV 53u
#endif

#ifndef KWS_UART_SPIN_MAX
#define KWS_UART_SPIN_MAX 20000u
#endif

#ifndef KWS_UART_LOG_EN
// 默认关闭：UART 打印会显著拖慢仿真，导致 I2S FIFO overflow。
#define KWS_UART_LOG_EN 0
#endif

#ifndef KWS_GPIO_STATUS_EN
// 默认开启：用 GPIO 输出状态/结果，TB 可据此判断进度/结束/卡死。
#define KWS_GPIO_STATUS_EN 1
#endif

#ifndef KWS_GPIO_ACTIVITY_EN
// 默认关闭进度型 GPIO 更新，避免板上 LED 在无检测时持续跳变。
#define KWS_GPIO_ACTIVITY_EN 0
#endif

#ifndef KWS_GPIO_LED_RESULT_MODE
// 板上实时 KWS 模式：detect 后直接把类别映射成 8-bit LED mask。
#define KWS_GPIO_LED_RESULT_MODE 1
#endif

#ifndef KWS_LED_ACTIVE_LOW
#define KWS_LED_ACTIVE_LOW 1
#endif

#ifndef KWS_RUN_TESTVECTOR
// 默认关闭：TV 会额外跑一次 NPU（读 uops/params/weight），在 XIP + RTL 仿真里非常耗时。
// 需要时可构建时打开：make ... KWS_RUN_TESTVECTOR=1
#define KWS_RUN_TESTVECTOR 0
#endif

#ifndef KWS_SRAM_BYTES
#define KWS_SRAM_BYTES (64u * 1024u)
#endif

#ifndef KWS_NUM_CLASSES
#define KWS_NUM_CLASSES 12u
#endif

#ifndef KWS_TIMEOUT_CYCLES
#define KWS_TIMEOUT_CYCLES 200000000u
#endif

#ifndef KWS_STATUS_GPIO_MASK
#define KWS_STATUS_GPIO_MASK 0xFFu
#endif

// GPIO 状态编码（gpio[7:4]=state, gpio[3:0]=payload）
// - 0x0?: boot/early
// - 0x1?: testvector
// - 0x2?: frontend init
// - 0x3?: streaming (payload 可放 frame_idx/sample_count 低 4bit)
// - 0x4?: audio_ready / infer scheduling
// - 0x8?: done (payload=top1_idx[3:0])
// - 0xF?: fail (payload=reason low 4bit)
#define KWS_GPIO_STATE_BOOT      0x0u
#define KWS_GPIO_STATE_TV        0x1u
#define KWS_GPIO_STATE_FE_INIT   0x2u
#define KWS_GPIO_STATE_STREAM    0x3u
#define KWS_GPIO_STATE_INFER     0x4u
#define KWS_GPIO_STATE_DONE      0x8u
#define KWS_GPIO_STATE_FAIL      0xFu

#ifndef KWS_HEARTBEAT_EN
#define KWS_HEARTBEAT_EN 1
#endif

#ifndef KWS_HEARTBEAT_MS
// 周期性打印“alive”日志，便于在长时间仿真/跑板时确认程序没有卡死。
// 注意：UART 打印本身会拖慢仿真；建议保持 1~5 秒级别，不要太小。
#define KWS_HEARTBEAT_MS 2000u
#endif

#ifndef KWS_TIMER_PRESCALER_LIMIT
#define KWS_TIMER_PRESCALER_LIMIT 255u
#endif

#ifndef KWS_CORE_CLK_HZ
#define KWS_CORE_CLK_HZ 50000000u
#endif

#ifndef KWS_ACT_BASE
#define KWS_ACT_BASE 0x00001000u
#endif

#ifndef KWS_PARAM_BASE
#define KWS_PARAM_BASE 0x00120000u
#endif

#ifndef KWS_MSM261S_PCLK_HZ
#define KWS_MSM261S_PCLK_HZ 50000000u
#endif

// 仿真提速：覆盖 MSM261S 的 I2S divider（不改变采样数据本身，只是更快把 PCM 喂进 FIFO）。
// - 不定义：按目标采样率计算 divider（例如 16kHz -> ~1 秒音频需要 ~1 秒仿真时间）
// - 定义为 0：最快（Fs = PCLK / (2*(0+1)*64) ≈ 781.25kHz @100MHz）
// 用法：
//   make ... FAST_I2S=1   (Makefile 会传 -DKWS_I2S_DIV_OVERRIDE=0)
#ifndef KWS_I2S_DIV_OVERRIDE
// #define KWS_I2S_DIV_OVERRIDE 0u
#endif

// 仿真提速：跳过 I2S+frontend，直接复用 testvector 写入的 input buffer，
// Run the FreeRTOS infer task once using the existing input buffer.
// 用法：make ... FAST_RT=1
#ifndef KWS_FAST_RT
#define KWS_FAST_RT 0
#endif

#ifndef KWS_AUDIO_QUEUE_LEN
#define KWS_AUDIO_QUEUE_LEN 2u
#endif

#ifndef KWS_TASK_STACK_AUDIO
#define KWS_TASK_STACK_AUDIO 512u
#endif

#ifndef KWS_TASK_STACK_INFER
#define KWS_TASK_STACK_INFER 1024u
#endif

#ifndef KWS_AUDIO_DC_REMOVE
// 默认关闭：保持与训练/离线特征提取一致（train_kws.py 的 FeatureExtractor 未做 DC remove）。
#define KWS_AUDIO_DC_REMOVE 0
#endif

#ifndef KWS_AUDIO_ENABLE_AGC
// 默认关闭：保持与训练/离线特征提取一致，且避免额外计算开销。
#define KWS_AUDIO_ENABLE_AGC 0
#endif

#ifndef KWS_AGC_TARGET_RMS_Q16
#define KWS_AGC_TARGET_RMS_Q16 0
#endif

#ifndef KWS_AGC_MAX_GAIN_Q16
#define KWS_AGC_MAX_GAIN_Q16 (64 << 16)
#endif

#ifndef KWS_INPUT_SCALE_Q16
// 0.127225593 * 2^16 ≈ 8338
#define KWS_INPUT_SCALE_Q16 8338
#endif

#ifndef KWS_VAD_ENABLE
#define KWS_VAD_ENABLE 1
#endif

#ifndef KWS_VAD_AVG_ABS_TH
// 16-bit PCM 1s window average absolute amplitude threshold.
#define KWS_VAD_AVG_ABS_TH 256u
#endif

#ifndef KWS_VAD_PEAK_TH
// Require a reasonable peak as well as average energy to avoid idle noise.
#define KWS_VAD_PEAK_TH 2048u
#endif

#ifndef KWS_VAD_TRIGGER_SAMPLES
// Lightweight listen window before running frontend/NPU.
#define KWS_VAD_TRIGGER_SAMPLES 1024u
#endif

#ifndef KWS_VAD_TRIGGER_AVG_ABS_TH
#define KWS_VAD_TRIGGER_AVG_ABS_TH 128u
#endif

#ifndef KWS_VAD_TRIGGER_PEAK_TH
#define KWS_VAD_TRIGGER_PEAK_TH 1024u
#endif

#ifndef KWS_VAD_PREROLL_SAMPLES
// Keep a small history before trigger so short commands do not lose their onset.
#define KWS_VAD_PREROLL_SAMPLES 2048u
#endif

#ifndef KWS_CONF_SCORE_TH
// INT8 output-logit threshold. Speaker playback on GW5A is notably weaker than near-field speech.
#define KWS_CONF_SCORE_TH 10
#endif

#ifndef KWS_CONF_MARGIN_TH
// Minimum top1-top2 INT8 logit margin.
#define KWS_CONF_MARGIN_TH 1
#endif

#ifndef KWS_NOISE_CLASS_IDX
#define KWS_NOISE_CLASS_IDX 10u
#endif

#ifndef KWS_SILENCE_CLASS_IDX
#define KWS_SILENCE_CLASS_IDX 11u
#endif

#ifndef KWS_LOG_RAW_TOP1
// Keep board UART quiet by default; enable for model/debug calibration.
#define KWS_LOG_RAW_TOP1 0
#endif

#ifndef KWS_LOG_REJECTED_EVERY
// Print one rejected/silent window every N rejects when UART logging is enabled.
#define KWS_LOG_REJECTED_EVERY 8u
#endif

#ifndef KWS_LOG_MIC_OVERFLOW_EVERY
// Keep UART from making FIFO pressure worse; set nonzero for debugging.
#define KWS_LOG_MIC_OVERFLOW_EVERY 0u
#endif

// 注意：当 ADDRESS_MODE_OFFSET==1 时，PARAM_BASE/INPUT_BASE/OUTPUT_BASE 是“偏移”，不等于链接地址。
// 参数 blob 的真实物理地址由 linker.ld 的 `.venus_params` 固定到 `KWS_PARAM_BASE`。

#if defined(VC_HAS_PLAN) && (VC_HAS_PLAN != 0u)
#define VENUS_ACT_BUF_BYTES                                                  \
  ((VC_PLAN_ARENA_BYTES > ACTIVATION_PEAK_BYTES) ? VC_PLAN_ARENA_BYTES        \
                                                 : ACTIVATION_PEAK_BYTES)
#else
#define VENUS_ACT_BUF_BYTES (ACTIVATION_PEAK_BYTES)
#endif

#endif
