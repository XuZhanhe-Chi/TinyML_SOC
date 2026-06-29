# Tang Primer 25K 上板 / Bring-Up

## 1. 硬件连接

本工程目标开发板为 **Sipeed Tang Primer 25K / GW5A-25A**。板上 FT2232 的 channel A 负责 JTAG/Flash，channel B 负责 UART。Linux 未加载 `ftdi_sio` 时不会出现 `/dev/ttyUSB*`，工程默认由 PyFtdi 直接访问 channel B，不需要 root。

| 功能 | FPGA pin | 主机侧 |
|---|---|---|
| 50 MHz clock | `E2` | Tang Primer 25K 板载时钟 |
| UART TX / RX | `C3` / `B3` | FT2232 channel B，115200 8N1 |
| I2S SCK / SD / WS | `C10` / `B10` / `G10` | Tang Primer 25K 板载数字麦克风 |
| QSPI CLK / CS | `E7` / `E6` | Tang Primer 25K 板载 SPI Flash |
| GPIO LED[7:0] | `A11 A10 E11 E10 K11 L11 L5 K5` | Tang Primer 25K 板载 8 LED |

主机检查：

```bash
lsusb
python3 -c 'from pyftdi.ftdi import Ftdi; print(Ftdi.list_devices("ftdi:///?"))'
```

测试扬声器或耳机应靠近板载麦克风，避免直接电气连接到 I2S 引脚。

## 2. 工具链

- Gowin IDE command-line tools。
- Gowin Programmer CLI。
- RISC-V bare-metal GCC。
- Python 3、`pyserial`、`espeak-ng` 和 PulseAudio `paplay`。

使用环境变量，不在仓库内写本机绝对路径：

```bash
export GOWIN_IDE_BIN=/path/to/Gowin/IDE/bin
export GOWIN_PROGRAMMER=/path/to/Gowin/Programmer/bin/programmer_cli
export RISCV_TOOLCHAIN_PREFIX=/path/to/riscv-none-elf-
export SERIAL_PORT=ftdi://ftdi:2232h/2
export SERIAL_BAUD=115200
```

检查环境不会安装或修改系统软件：

```bash
make setup
make env
```

## 3. 构建

```bash
make soc-rtl
make gw5a-fw
make gw5a-bitstream
```

关键输出：

```text
build/rtl/VenusCoreRVTop.v
build/fw/kws_xip_rt_flash.bin
build/fw/kws_xip_rt_boot_[0-3].memb
build/gowin/tinyml_soc_gw5a/impl/pnr/tinyml_soc_gw5a.fs
```

成功标准：固件布局检查通过；Flash image 小于 4 MiB；`clk` actual Fmax 不低于 50 MHz；没有未驱动端口或缺少 initmem 文件的 warning。

## 4. 下载器与 Flash

```bash
scripts/gw5a_program.sh scan
make gw5a-probe
make gw5a-detect-flash
```

脚本会自动选择下载器，并用文件锁阻止两个 Programmer 进程同时访问 JTAG。多个下载器并存时显式指定：

```bash
export GOWIN_CABLE_INDEX=<index>
export GOWIN_CHANNEL=<channel>
```

按固定布局烧录：

```bash
make gw5a-flash-bitstream  # external Flash 0x000000
make gw5a-flash-kws        # external Flash 0x400000
make gw5a-reboot
```

每一步都必须看到 `Program and Verify Flash successfully` 或 `Finished`。脚本同时检查 CLI 输出中的 `Error:`，即使工具错误地返回退出码 0 也会失败。

## 5. 串口启动验收

使用内核 tty 时，为捕获 boot 行应先启动 monitor，再执行 reboot。

终端 A：

```bash
SERIAL_PORT=ftdi://ftdi:2232h/2 make monitor
```

终端 B：

```bash
make gw5a-reboot
```

PyFtdi 不能与 Gowin Programmer 稳定并发访问同一 FT2232。`make gw5a-demo-check` 会先清空 channel B、释放 USB、执行 reboot，再立即打开 channel B；FT2232 缓冲足以保存短启动日志。

预期启动序列：

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

## 6. 音频与 KWS 验收

单独播放固定测试词：

```bash
make play-kws-test
```

默认使用 `espeak-ng` 播放两轮 `one two up down`。可覆盖测试集合：

```bash
KWS_TEST_WORDS='one two three four' KWS_TEST_REPEAT=3 make play-kws-test
```

可要求多条结果和指定类别至少出现一次：

```bash
DEMO_MIN_DETECTS=4 DEMO_EXPECT_LABELS='one up down' \
KWS_TEST_WORDS='one two up down' KWS_TEST_REPEAT=2 \
SERIAL_PORT=ftdi://ftdi:2232h/2 make gw5a-demo-check
```

完整自动验收会协调 FPGA 重配置和串口打开顺序，等待 ready、播放测试词，并要求合法 detect：

```bash
SERIAL_PORT=ftdi://ftdi:2232h/2 make gw5a-demo-check
```

日志保存到忽略目录 `build/board/gw5a_demo.log`。通过条件：

- 捕获 `[TinyML_SOC] ready mic_sr=...`。
- 捕获至少一行完整 `[KWS] detect ... cycles=... us=...`。
- 串口没有 `[TinyML_SOC][ERR]`。
- 对应命令更新 LED；reject 不改变上次结果。

## 7. 冷启动复查

烧录成功后断开并重新接通板卡电源，不运行 Programmer，只打开 UART。仍应得到相同 boot/ready 序列。这一步证明 FPGA 配置和 KWS 固件都来自 external Flash，而不是临时 SRAM 配置。

## English Summary

Build the RTL, firmware, and bitstream for Sipeed Tang Primer 25K (GW5A-25A); verify both FPGA and external Flash IDs; program the bitstream at `0x000000` and KWS image at `0x400000`; then run the automated UART and audio check.
