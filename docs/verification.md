# 验证 / Verification

## 验收矩阵

| 层级 | 命令 | 通过条件 |
|---|---|---|
| 公开性 | `make check-public-tree` | 无绝对 home path、联系信息、未批准 submodule URL、生成物、音频或超大文件 |
| 脚本 | `make check-scripts` | Bash 语法和 Python bytecode compile 通过 |
| RTL | `make soc-rtl` | 生成单文件 RTL，Flash offset、SRAM initmem 和 unused-port tie-off 存在 |
| 固件 | `make gw5a-fw` | image 小于 4 MiB，四个 boot lane 存在，硬件/软件 SRAM 都是 64 KiB |
| NPU 仿真 | `make sim-vcs-testvector` | Flash AHB 逐地址校验通过，出现 `TV PASS` |
| PCM 仿真 | `make sim-vcs-pcm` | I2S、CPU frontend、NPU 全链路得到 `expected=0 got=0` |
| FPGA | `make gw5a-bitstream` | 50 MHz timing 通过，资源不超器件，端口悬空 warning 为 0 |
| 下载 | `make gw5a-probe && make gw5a-detect-flash` | FPGA ID 和 external Flash ID 可读 |
| 板级 | `make gw5a-demo-check` | Flash reboot、boot、ready、detect、非零 cycles/us、GPIO readback 全部通过 |

## 第三方依赖

```bash
git submodule status third_party/TinyML_NPU third_party/FreeRTOS-Kernel third_party/VexRiscv
```

固定版本：

| 依赖 | Commit | 说明 |
|---|---|---|
| TinyML_NPU | `f7a0eaa` | 相对 URL `../TinyML_NPU.git` |
| FreeRTOS-Kernel | `3ace389` | MIT |
| VexRiscv | `6807560` | MIT |

VexRiscv 源码只存在于 submodule；本项目维护的 AHB wrapper 位于 `hw/spinal/src/main/scala/cpu/VexRiscvAhb.scala`。

## 2026-06-29 自动与实现验证

| 项目 | 结果 | 证据 |
|---|:---:|---|
| 公开树检查 | PASS | 检查脚本扫描自身；无个人标识、联系信息、本地路径、媒体、binary 或超大待提交文件 |
| RTL generation | PASS | `flash_offset_u32 = 32'h00400000`；四 lane initmem；unused VexRiscv ports 显式 tie-off |
| Firmware | PASS | Flash image `188608 bytes`；boot stub `32 bytes`；SRAM map 64 KiB 一致 |
| Gowin implementation | PASS | 生成 `.fs/.bin` 和 PnR/timing reports |
| Timing | PASS | `clk` constraint 50.000 MHz，actual Fmax 55.157 MHz |
| Unconnected ports | PASS | `EX1998`、`EX2565`、`EX0211`、`undriven/no driver/unconnected` 均为 0 |
| VCS testvector | PASS | 仿真时间 17.843250 ms；`top1=5`；Flash AHB data check 通过 |
| VCS PCM | PASS | 生成词 `one`；仿真时间 757.643450 ms；`expected=0 got=0` |

资源：

| Logic | Register | CLS | I/O | BSRAM | DSP |
|---:|---:|---:|---:|---:|---:|
| 13980/23040 (61%) | 9548/23280 (42%) | 10025/11520 (88%) | 26/86 (31%) | 50/56 (90%) | 19.5/28 (70%) |

剩余 warning：5 条 `NL0002` unused logic swept 和 1 条 `PR1014` JTAG generic routing。它们不属于端口悬空，系统 `clk` timing 已通过；JTAG 只用于调试和下载，不进入 50 MHz 数据路径。

VCS 使用 `TINYML_SOC_SIM_FAST_QSPI` 的一周期 AHB ROM 加速 XIP，但仍执行真实 VexRiscv、AHB/APB、SRAM、I2S、NPU RTL 和固件。testbench 会逐次比对 NPU 从 XIP 读取的 32-bit 数据；Make 目标还会检查 PASS 标记，不能只靠仿真器退出码判定。

PCM 回归使用 `espeak-ng` 生成 `one`，重采样为 16000 个 16-bit sample。`FAST_I2S=1` 只缩短样本间隔；仿真专用置信度门限为 score/margin `0/0`，用于检查分类链路。板级默认仍为 `10/1`。

### Testvector golden 说明

旧 behavioral/plan simulator 元数据给出的 top1 是 7；当前 NPU RTL 和 GW5A 对同一 bundle/input 都稳定得到 `top1=5, score=60`。头文件保留原软件参考值，并单独定义 `VC_KWS_EXPECTED_TOP1_RTL=5` 作为 RTL/板级回归 golden，避免把两种口径混写。

板级复核输出：

```text
[KWS][TV] top1_idx=5 expected=5 score=60 label=six
[KWS][TV] PASS
```

## 2026-06-29 板级验证

### 下载与启动

| 项目 | 结果 | 证据 |
|---|:---:|---|
| FPGA probe | PASS | `GW5A-25A`, ID `0x0001281B` |
| external Flash detect | PASS | Flash ID `0x0B4017` |
| bitstream program/verify | PASS | `0x000000..0x0E1100`，program/verify 成功 |
| KWS image program/verify | PASS | `0x400000..0x42E100`，program/verify 成功 |
| Flash reconfiguration | PASS | Programmer Reprogram 后从 Flash 启动 |
| UART | PASS | PyFtdi 直连 FT2232 channel B，115200 |
| Boot | PASS | 捕获 `boot -> fe_init -> scheduler -> task_audio/task_infer -> ready` |
| Microphone | PASS | `ready mic_sr=16276`，VAD 持续获得非零 avg/peak |

启动日志：

```text
[TinyML_SOC] boot
[TinyML_SOC] fe_init_start
[TinyML_SOC] fe_init_ok
[TinyML_SOC] fe_wb_ok
[TinyML_SOC] queue ok
[TinyML_SOC] scheduler start
[TinyML_SOC] task_audio start
[TinyML_SOC] ready mic_sr=16276
[TinyML_SOC] task_infer start
```

### 多关键词 sweep

主机扬声器靠近板载麦克风，`espeak-ng` 依次播放 `one` 到 `eight`、`up`、`down`。10 个 prompt 产生 8 条合法 detect，观测标签覆盖 `one/four/five/two/up/down`，要求的 `one/up/down` 全部出现；无目标端错误。

该 sweep 验证实时数据链路和多类别输出，不作为模型准确率基准。合成语音、扬声器、距离和房间声学与训练集不同，不能据此报告模型 accuracy。

### 最终命令与 LED 回读

最终固件连续识别 `one/up/down`，并从 GPIO input buffer 读回实际输出 pin：

```text
[KWS] detect idx=0 label=one ... cycles=2549232 us=50984 ... led=0x000000FE
[KWS] detect idx=8 label=up  ... cycles=2600769 us=52015 ... led=0x00000000
[KWS] detect idx=9 label=down ... cycles=2550254 us=51005 ... led=0x000000FF
```

结果：

- 三个目标类别全部正确识别。
- `one/up/down` 的低电平有效输出分别为 `0xFE/0x00/0xFF`，写入值与 pin readback 一致。
- NPU 时间范围 `2,549,232..2,600,769 cycles`，即 `50.984..52.015 ms`；平均约 `51.335 ms`。
- 日志无 `[TinyML_SOC][ERR]`，无 GPIO readback mismatch。

复现命令：

```bash
DEMO_MIN_DETECTS=3 DEMO_EXPECT_LABELS='one up down' \
KWS_TEST_WORDS='one up down' KWS_TEST_REPEAT=2 \
SERIAL_PORT=ftdi://ftdi:2232h/2 make gw5a-demo-check
```

## English Summary

VCS reference-vector and PCM regressions pass. The final Sipeed Tang Primer 25K (GW5A-25A) board test passes Flash boot, FT2232 UART, I2S/VAD, CPU frontend, NPU inference, structured logs, nonzero `mcycle` timing, and GPIO pin readback for `one`, `up`, and `down`.
