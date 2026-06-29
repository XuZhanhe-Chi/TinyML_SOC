# 故障排查 / Troubleshooting

## Programmer 找不到或挂起

```bash
scripts/gw5a_program.sh scan
make gw5a-probe
make gw5a-detect-flash
```

脚本会自动选择支持的 cable，FT2232 `USB Debugger A` 使用 `cable-index=4, channel=0`。多个下载器并存时设置：

```bash
export GOWIN_CABLE_INDEX=<index>
export GOWIN_CHANNEL=<channel>
```

PyFtdi 关闭 channel B 后，D2XX 第一次打开 channel A 偶尔会超时。脚本对短操作设置 15 秒硬超时并自动重试一次；不要同时运行两个 Programmer 进程。

出现 error 50 时先顺序执行 probe 和 Flash detect，不要并行调用 Programmer。Flash ID 可读后再烧录。

## 没有 `/dev/ttyUSB*`

当前板卡 UART 使用 FT2232 channel B，不要求内核 `ftdi_sio`。推荐直接使用：

```bash
export SERIAL_PORT=ftdi://ftdi:2232h/2
make monitor
```

确认 PyFtdi 和 USB 权限：

```bash
python3 -c 'from pyftdi.ftdi import Ftdi; print(Ftdi.list_devices("ftdi:///?"))'
```

如果列表为空，检查 USB 连接、udev 权限和是否有其他进程占用 interface 1。

## 无 boot/ready

检查：

- UART pin 是否为 TX `C3`、RX `B3`，baud 是否为 115200。
- bitstream 和 KWS image 是否分别写入 `0x000000`、`0x400000`。
- 烧录后是否执行 `make gw5a-reboot`。
- 四个 boot initmem lane 是否在 bitstream 构建前生成。
- RTL 是否包含 SRAM `$readmemb` block 和 `flash_offset_u32 = 32'h00400000`。

## Illegal instruction

不要把 reset vector 直接指向 XIP。重新生成 boot stub 和 bitstream：

```bash
make gw5a-fw
make soc-rtl
make gw5a-bitstream
```

若只在 fast-QSPI 仿真出现 illegal instruction，确认 RTL 和 testbench 都以 `TINYML_SOC_SIM_FAST_QSPI` 编译。加速模型必须寄存 AHB 地址相位并在下一数据相位组合输出，不能再额外寄存一次 HRDATA。

## VCS 无许可证或仿真未通过

VCS 报 `Failed to obtain license` 属于许可证服务问题，不是 RTL 编译结果。许可证恢复后重新运行：

```bash
make sim-vcs-testvector
make sim-vcs-pcm
```

Make 目标会在日志中强制查找 `TV PASS` 或 `PASS expected=`。VCS 对 `$fatal` 的进程退出码不能单独作为通过依据。

## Ready 后无 detect

检查：

- `mic_sck/mic_sd/mic_ws` 是否为 `C10/B10/G10`。
- 串口是否有 `vad_trigger`，以及 avg/peak 是否为非零。
- 扬声器距离和音量是否足以超过 VAD threshold。
- 是否出现 `mic_overflow`、`npu_failed` 或 frontend error。
- 需要时只在调试构建中调整 VAD threshold，不要把未验证阈值作为默认值提交。

## `cycles=0` 或明显过小

最终 bitstream 必须包含启用 `mcycle` 的 VexRiscv，最终固件必须使用相同版本。重新构建并同时烧录两者；只更新固件不能为旧 CPU 配置增加 CSR。

正常 KWS NPU 延迟约 2.5M cycles，不应是 0 或两位数。

## GPIO readback mismatch

目标端会报告：

```text
[TinyML_SOC][ERR] gpio_readback expected=... actual=...
```

检查 GPIO constraints、LED 电平极性、output enable 和板上负载。默认低电平点亮，`one/up/down` 的 pin 值应为 `0xFE/0x00/0xFF`。

## Flash 镜像重叠

```bash
stat -c '%s %n' \
  build/gowin/tinyml_soc_gw5a/impl/pnr/tinyml_soc_gw5a.bin \
  build/fw/kws_xip_rt_flash.bin
```

KWS image 必须小于 4 MiB，并从 `0x400000` 开始。布局不确定时停止烧录，先检查 [flash-layout.md](flash-layout.md)。

## English Summary

Use FT2232 channel A for JTAG and channel B through PyFtdi for UART. Run Programmer operations sequentially, keep the fixed Flash offsets, and treat target `[ERR]` lines as hard failures.
