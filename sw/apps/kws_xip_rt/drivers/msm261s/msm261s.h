/**
 * @file msm261s.h
 * @brief Board Support Package for MSM261S I2S APB Controller
 * @version 1.0
 * @date 2023-10-27
 */

#ifndef _MSM261S_H_
#define _MSM261S_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Register Definition                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief MSM261S Register Map Structure
 */
typedef struct {
    volatile uint32_t CTRL;     /*!< Offset: 0x000 Control Register */
    volatile uint32_t DIV;      /*!< Offset: 0x004 Divider Register */
    volatile uint32_t STATUS;   /*!< Offset: 0x008 Status Register */
    volatile uint32_t DATA;     /*!< Offset: 0x00C Data Register (FIFO) */
} MSM261S_TypeDef;

/* CTRL Register Bits */
#define MSM261S_CTRL_EN_Pos         (0U)
#define MSM261S_CTRL_EN_Msk         (1U << MSM261S_CTRL_EN_Pos)
#define MSM261S_CTRL_SOFT_RST_Pos   (1U)
#define MSM261S_CTRL_SOFT_RST_Msk   (1U << MSM261S_CTRL_SOFT_RST_Pos)
#define MSM261S_CTRL_CLR_OVF_Pos    (2U)
#define MSM261S_CTRL_CLR_OVF_Msk    (1U << MSM261S_CTRL_CLR_OVF_Pos)

/* DIV Register Bits */
#define MSM261S_DIV_VAL_Pos         (0U)
#define MSM261S_DIV_VAL_Msk         (0xFFFFU << MSM261S_DIV_VAL_Pos)

/* STATUS Register Bits */
#define MSM261S_STATUS_EMPTY_Pos    (0U)
#define MSM261S_STATUS_EMPTY_Msk    (1U << MSM261S_STATUS_EMPTY_Pos)
#define MSM261S_STATUS_FULL_Pos     (1U)
#define MSM261S_STATUS_FULL_Msk     (1U << MSM261S_STATUS_FULL_Pos)
#define MSM261S_STATUS_OVF_Pos      (2U)
#define MSM261S_STATUS_OVF_Msk      (1U << MSM261S_STATUS_OVF_Pos)
#define MSM261S_STATUS_LEVEL_Pos    (8U)
#define MSM261S_STATUS_LEVEL_Msk    (0xFFFFU << MSM261S_STATUS_LEVEL_Pos) // Mask covers up to bit 23

/* -------------------------------------------------------------------------- */
/* Driver Types                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief MSM261S Device Handle
 */
typedef struct {
    MSM261S_TypeDef *Instance;  /*!< Register base address */
    uint32_t PCLK_Hz;          /*!< AHB/System Clock Frequency in Hz */
} MSM261S_Handle_t;

/* -------------------------------------------------------------------------- */
/* API Functions                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Initialize the MSM261S controller.
 * @param  handle: Pointer to the device handle.
 * @param  base_addr: The physical base address of the AHB peripheral.
 * @param  pclk_hz: The system clock frequency connected to hclk.
 * @param  sample_rate_hz: Target audio sampling rate (e.g., 16000, 44100).
 * @return Actual configured sample rate (Hz), or 0 if error.
 */
uint32_t MSM261S_Init(MSM261S_Handle_t *handle, uintptr_t base_addr, uint32_t pclk_hz, uint32_t sample_rate_hz);

/**
 * @brief  Enable or Disable the audio capture.
 * @param  handle: Pointer to the device handle.
 * @param  enable: true to enable, false to disable.
 */
void MSM261S_Cmd(MSM261S_Handle_t *handle, bool enable);

/**
 * @brief  Check if FIFO has data.
 * @param  handle: Pointer to the device handle.
 * @return true if FIFO is NOT empty, false otherwise.
 */
bool MSM261S_HasData(MSM261S_Handle_t *handle);

/**
 * @brief  Get the number of samples currently available in FIFO.
 * @note   This returns the number of int16_t samples (FIFO words * 2).
 * @param  handle: Pointer to the device handle.
 * @return Number of samples.
 */
uint32_t MSM261S_GetSampleCount(MSM261S_Handle_t *handle);

/**
 * @brief  Read audio samples from FIFO.
 * @note   This function handles the unpacking (2 samples per 32-bit read).
 * @param  handle: Pointer to the device handle.
 * @param  pBuffer: Pointer to the destination buffer (int16_t).
 * @param  max_samples: Maximum number of samples to read (must be >= 2).
 * @return Number of samples actually read.
 */
uint32_t MSM261S_Read(MSM261S_Handle_t *handle, int16_t *pBuffer, uint32_t max_samples);

/**
 * @brief  Check and clear overflow flag.
 * @param  handle: Pointer to the device handle.
 * @return true if overflow occurred since last clear, false otherwise.
 */
bool MSM261S_CheckAndClearOverflow(MSM261S_Handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* _MSM261S_H_ */
