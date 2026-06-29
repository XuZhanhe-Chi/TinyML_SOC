# 第三方声明 / Third-Party Notices

本仓库自有源码默认使用 Apache-2.0，文件另有声明时以文件声明为准。第三方组件保持其原许可证和归属。

## TinyML_NPU

`TinyML_NPU` 作为 git submodule 使用，路径为 `third_party/TinyML_NPU`。其源码、生成物边界和许可证以该仓库为准。

## FreeRTOS-Kernel

`FreeRTOS-Kernel` 作为 git submodule 使用，路径为 `third_party/FreeRTOS-Kernel`，固定到 commit `3ace389`。许可证为 MIT。

## VexRiscv

`VexRiscv` 作为 git submodule 使用，路径为 `third_party/VexRiscv`。许可证为 MIT。

本项目的 VexRiscv AHB wrapper 位于 `hw/spinal/src/main/scala/cpu/VexRiscvAhb.scala`，属于 TinyML_SOC 自有集成代码。

## SpinalHDL

SpinalHDL 通过 SBT 依赖使用，不 vendored 到本仓库。

## GOWIN Tools 和生成文件

GOWIN IDE 与 Programmer 是外部工具，不随本仓库分发。bitstream、programmer file、firmware binary、log、report、waveform 和 implementation directory 都是生成产物，不提交到 Git。

## Trademarks

GOWIN、RISC-V、FreeRTOS 等名称仅用于说明兼容性，不代表背书。
