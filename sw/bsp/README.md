# TinyML_SOC BSP

本目录提供 VexRiscv 侧的最小 BSP，用于 `sw/apps/kws_xip_rt/` 固件。驱动只封装当前 SoC 已实现的寄存器接口，不包含板外设备抽象。

## 地址定义

`core/soc.h` 是软件侧地址映射的唯一入口，必须与 `docs/memory-map.md` 和 SpinalHDL 顶层保持一致。

## 驱动

- UART：`drivers/uart/uart.h`
- GPIO：`drivers/gpio/gpio.h`
- GPIO IRQ：`drivers/gpio/gpio_irq.h`
- IRQ controller：`drivers/irq/irq_ctrl.h`
- Timer：`drivers/timer/murax_timer.h`
- TinyML_NPU：`drivers/venus/venus_driver.h`

I2S 麦克风驱动与应用绑定较紧，位于 `sw/apps/kws_xip_rt/drivers/msm261s/`。
