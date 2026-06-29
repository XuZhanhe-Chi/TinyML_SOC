/**
 * @file msm261s.c
 * @brief Implementation of MSM261S I2S APB Driver
 */

#include "msm261s.h"
#include <stddef.h>

/**
 * @brief  Calculate Divider for target sample rate
 * Formula: F_sample = F_hclk / (2 * (DIV + 1) * 64)
 * => DIV = (F_hclk / (128 * F_sample)) - 1
 */
static uint32_t msm261s_calc_div(uint32_t pclk, uint32_t sample_rate)
{
    if (sample_rate == 0) return 0;

    // (DIV + 1) ~= pclk / (128 * fs)
    // Use rounding-to-nearest to minimize sample-rate error.
    const uint32_t denominator = 128u * sample_rate;
    uint32_t div_plus1 = (pclk + (denominator / 2u)) / denominator;
    if (div_plus1 == 0u) {
        div_plus1 = 1u;
    }
    uint32_t div = div_plus1 - 1u;

    // Clamp to 16-bit
    if (div > 0xFFFF) {
        div = 0xFFFF;
    }

    return div;
}

uint32_t MSM261S_Init(MSM261S_Handle_t *handle, uintptr_t base_addr, uint32_t pclk_hz, uint32_t sample_rate_hz)
{
    if (handle == NULL || base_addr == 0) {
        return 0;
    }

    handle->Instance = (MSM261S_TypeDef *)base_addr;
    handle->PCLK_Hz = pclk_hz;

    // 1. Disable first
    handle->Instance->CTRL &= ~MSM261S_CTRL_EN_Msk;

    // 2. Perform Soft Reset
    handle->Instance->CTRL |= MSM261S_CTRL_SOFT_RST_Msk;
    // Small delay might be needed depending on pclk, but logic is synchronous usually
    // Assuming single cycle pulse is enough or just writing back 0
    handle->Instance->CTRL &= ~MSM261S_CTRL_SOFT_RST_Msk;

    // 3. Clear Overflow
    handle->Instance->CTRL |= MSM261S_CTRL_CLR_OVF_Msk;
    handle->Instance->CTRL &= ~MSM261S_CTRL_CLR_OVF_Msk;

    // 4. Configure Divider
    uint32_t div_val = msm261s_calc_div(pclk_hz, sample_rate_hz);
    handle->Instance->DIV = div_val;

    // 5. Enable
    handle->Instance->CTRL |= MSM261S_CTRL_EN_Msk;

    // Return actual sample rate for user verification
    return pclk_hz / (2 * (div_val + 1) * 64);
}

void MSM261S_Cmd(MSM261S_Handle_t *handle, bool enable)
{
    if (enable) {
        handle->Instance->CTRL |= MSM261S_CTRL_EN_Msk;
    } else {
        handle->Instance->CTRL &= ~MSM261S_CTRL_EN_Msk;
    }
}

bool MSM261S_HasData(MSM261S_Handle_t *handle)
{
    return (handle->Instance->STATUS & MSM261S_STATUS_EMPTY_Msk) == 0;
}

uint32_t MSM261S_GetSampleCount(MSM261S_Handle_t *handle)
{
    uint32_t status = handle->Instance->STATUS;
    uint32_t fifo_words = (status & MSM261S_STATUS_LEVEL_Msk) >> MSM261S_STATUS_LEVEL_Pos;

    // Each FIFO word contains 2 samples
    return fifo_words * 2;
}

bool MSM261S_CheckAndClearOverflow(MSM261S_Handle_t *handle)
{
    if (handle->Instance->STATUS & MSM261S_STATUS_OVF_Msk) {
        // Clear flag
        handle->Instance->CTRL |= MSM261S_CTRL_CLR_OVF_Msk;
        handle->Instance->CTRL &= ~MSM261S_CTRL_CLR_OVF_Msk;
        return true;
    }
    return false;
}

uint32_t MSM261S_Read(MSM261S_Handle_t *handle, int16_t *pBuffer, uint32_t max_samples)
{
    uint32_t count = 0;

    // Need space for at least 2 samples to read one FIFO word
    while (count <= (max_samples - 2)) {
        // Check if FIFO is empty
        if (handle->Instance->STATUS & MSM261S_STATUS_EMPTY_Msk) {
            break;
        }

        // Read 32-bit data (contains 2 samples)
        uint32_t raw_data = handle->Instance->DATA;

        // Unpack:
        // Lower 16 bits = Older sample (Sample N)
        // Upper 16 bits = Newer sample (Sample N+1)
        int16_t sample_1 = (int16_t)(raw_data & 0xFFFF);
        int16_t sample_2 = (int16_t)((raw_data >> 16) & 0xFFFF);

        pBuffer[count++] = sample_1;
        pBuffer[count++] = sample_2;
    }

    return count;
}
