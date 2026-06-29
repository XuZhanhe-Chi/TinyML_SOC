#ifndef SW_VEXRISCV_BSP_CORE_SOC_H_
#define SW_VEXRISCV_BSP_CORE_SOC_H_

#include <stdint.h>

// ============================================================================
// VenusCoreRVTop memory map (current / only)
// ============================================================================
//
// 说明：
// - 本 BSP 只保留 `VenusCoreRVTop` 的地址映射（APB=0x0060_0000，外设 4KB 对齐）。
// - 如需兼容旧 demo SoC，请在历史版本中查看对应地址定义。
//

// On-chip SRAM window in hw
#define SOC_SRAM_BASE 0x00000000u
#define SOC_SRAM_SIZE (64u * 1024u)

// AHB windows
#define SOC_AHB_QSPI_BASE 0x00100000u
#define SOC_AHB_QSPI_SIZE (4u * 1024u * 1024u) // 4MB XIP window

#define SOC_AHB_APB_BASE 0x00600000u
#define SOC_AHB_APB_SIZE (1u * 1024u * 1024u)

// APB peripherals (4KB aligned, derived from `VenusCoreRVTop` implementation)
#define SOC_GPIO_BASE (SOC_AHB_APB_BASE + 0x0000u)
#define SOC_GPIO_IRQ_BASE (SOC_AHB_APB_BASE + 0x1000u)
#define SOC_UART_BASE (SOC_AHB_APB_BASE + 0x2000u)
#define SOC_TIMER_BASE (SOC_AHB_APB_BASE + 0x3000u)
#define SOC_IRQ_CTRL_BASE (SOC_AHB_APB_BASE + 0x4000u)
#define SOC_NPU_APB_BASE (SOC_AHB_APB_BASE + 0x5000u)
#define SOC_I2S_MIC_BASE (SOC_AHB_APB_BASE + 0x6000u)

#endif  // SW_VEXRISCV_BSP_CORE_SOC_H_
