# 性能与资源 / Performance

本文只记录可复现的配置值和实测结果。Gowin 报告属于构建产物，不提交；表中数据可通过 `make gw5a-bitstream` 重新生成。

## 实现结果

目标开发板：Sipeed Tang Primer 25K。目标器件：GW5A-25A，封装 `GW5A-LV25MG121NC1/I0`，系统时钟约束 50 MHz。

| 指标 | 使用量 | 占比 |
|---|---:|---:|
| Logic | 13980 / 23040 | 61% |
| Register | 9548 / 23280 | 42% |
| CLS | 10025 / 11520 | 88% |
| I/O | 26 / 86 | 31% |
| BSRAM | 50 / 56 | 90% |
| DSP | 19.5 / 28 | 70% |

时序结果：

| 时钟 | 约束 | Gowin Actual Fmax | 余量 |
|---|---:|---:|---:|
| `clk` | 50.000 MHz | 55.157 MHz | 5.157 MHz / 10.3% |

结论：当前镜像满足 50 MHz setup timing，但 CLS 和 BSRAM 分别达到 88% 和 90%，后续扩展应优先关注这两项。`55.157 MHz` 是该次布局布线结果，不是对所有构建种子或更高工作频率的承诺。

## 固件与模型

| 项目 | 大小或数量 |
|---|---:|
| Flash KWS image | 188608 bytes |
| SRAM boot stub | 32 bytes |
| NPU uOP | 24 条，768 bytes |
| INT8 参数 | 57344 bytes |
| activation arena peak | 32000 bytes |
| NPU input | 8000 bytes |
| NPU output | 12 bytes |

Flash KWS image 只占预留 4 MiB 区域约 4.5%，不会与 `0x000000` 开始的 FPGA 配置镜像重叠。

## 实时性口径

一次识别包含三个阶段：

1. 音频采集：16000 samples，在实际 `16276 Hz` 下约 0.983 s。
2. Frontend：CPU 在采样过程中逐帧计算，不应把全部 frontend 时间简单叠加在采集时间之后。
3. NPU：`detect` 行中的 `cycles` 和 `us` 由 VexRiscv `mcycle` 测量，从 START 写入前到 DONE 观测时，不包含采集、VAD 和 frontend。

串口字段：

```text
[KWS] detect idx=<n> label=<name> score=<n> margin=<n> cycles=<n> us=<n>
```

板级推理延迟必须从实际 `detect` 行记录，不能用 RTL active cycle、仿真时间或 Fmax 推算替代。当前实测数据见 [verification.md](verification.md)。

## 板级 NPU 延迟

最终固件对 `one/up/down` 的三次闭环测试：

| 样本 | Cycles | 延迟 |
|---|---:|---:|
| `one` | 2549232 | 50.984 ms |
| `up` | 2600769 | 52.015 ms |
| `down` | 2550254 | 51.005 ms |
| 平均 | 2566752 | 51.335 ms |

NPU-only 理论处理率约为 19.5 inference/s，但实时 demo 仍需约 0.983 s 音频窗口，并且当前固件不重叠推理与下一条命令，所以不能把 19.5 inference/s 当作端到端 KWS 吞吐。

## 复现

```bash
make gw5a-bitstream
python3 scripts/summarize_gowin_reports.py \
  build/gowin/tinyml_soc_gw5a/impl/pnr/tinyml_soc_gw5a.rpt.txt \
  build/gowin/tinyml_soc_gw5a/impl/pnr/tinyml_soc_gw5a_tr_content.html
```

## English Summary

The current Sipeed Tang Primer 25K (GW5A-25A) place-and-route result meets 50 MHz with an actual Fmax of 55.157 MHz. Board-measured NPU latency is about 51.3 ms; this excludes audio capture and CPU feature extraction.
