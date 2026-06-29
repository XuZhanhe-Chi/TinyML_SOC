# 地址映射 / Memory Map

所有地址均为 CPU/NPU 看到的 SoC 物理地址。总线宽度 32 bit，小端序。

## AHB 区域

| 区域 | Base | Size | CPU | NPU DMA | 用途 |
|---|---:|---:|:---:|:---:|---|
| SRAM | `0x00000000` | 64 KiB | R/W/X | R/W | boot stub、stack、activation、frontend work buffer |
| QSPI XIP | `0x00100000` | 4 MiB | R/X | R | firmware text、常量、uOP、INT8 参数 |
| APB bridge | `0x00600000` | 1 MiB | R/W | 禁止 | memory-mapped peripherals |

NPU DMA 对 APB 的访问在 AHB interconnect 中被拒绝。NPU 配置必须由 CPU 访问 APB register 完成。

## APB 外设

| 外设 | Base | Window | 主要用途 |
|---|---:|---:|---|
| GPIO | `0x00600000` | 4 KiB | 8 个结果 LED |
| GPIO IRQ | `0x00601000` | 4 KiB | GPIO 边沿/电平中断 |
| UART | `0x00602000` | 4 KiB | 115200 结构化日志 |
| Timer | `0x00603000` | 4 KiB | FreeRTOS tick、NPU cycle timing |
| IRQ controller | `0x00604000` | 4 KiB | GPIO/UART/Timer/NPU 聚合 |
| TinyML_NPU APB | `0x00605000` | 4 KiB | uOP base/count、start、status、IRQ |
| I2S microphone | `0x00606000` | 4 KiB | clock divider、FIFO status/data |

## NPU 寄存器

以下 offset 相对 `0x00605000`：

| Offset | 名称 | 访问 | 说明 |
|---:|---|:---:|---|
| `0x000` | `UOP_BASE` | R/W | uOP 物理地址 |
| `0x004` | `CTRL` | R/W | bit0 start，bit1 abort，bit2 reset |
| `0x008` | `STATUS` | R | busy、error、opcode、error code |
| `0x00C` | `VERSION` | R | 硬件版本 |
| `0x010` | `UOP_COUNT` | R/W | uOP 数量 |
| `0x020` | `INT_EN` | R/W | done/error enable |
| `0x024` | `INT_STATUS` | RW1C | done/error status |
| `0x080..0x0A0` | `DEBUG[0..8]` | R | 调试状态 |
| `0x0A4` | `DEBUG_CTRL` | RW1P | 调试脉冲控制 |

## I2S 麦克风寄存器

以下 offset 相对 `0x00606000`：

| Offset | 名称 | 访问 | 说明 |
|---:|---|:---:|---|
| `0x00` | `CTRL` | R/W | enable、soft reset、clear overflow |
| `0x04` | `DIV` | R/W | 16-bit SCK divider |
| `0x08` | `STATUS` | R | empty、full、overflow、FIFO word level |
| `0x0C` | `DATA` | R | 读出并弹出两个 16-bit PCM samples |

采样率公式：

```text
Fs = PCLK / (2 * (DIV + 1) * 64)
```

50 MHz、目标 16 kHz 时 `DIV=23`，实际 `Fs=16276 Hz`。

## Flash 映射

```text
SoC 0x00100000 -> external Flash 0x400000
SoC 0x004FFFFF -> external Flash 0x7FFFFF
```

FPGA 配置镜像从 external Flash `0x000000` 开始，KWS XIP image 从 `0x400000` 开始，互不覆盖。

## 一致性来源

- 硬件：`hw/spinal/src/main/scala/config/VenusCoreRVTopConfig.scala`
- 软件：`sw/bsp/core/soc.h`
- 文档：本文件
- 自动检查：`scripts/check_fw_layout.sh`

## English Summary

The SoC exposes 64 KiB SRAM, a 4 MiB QSPI XIP window, and a 1 MiB APB window. NPU DMA can access SRAM and XIP but is explicitly blocked from APB.
