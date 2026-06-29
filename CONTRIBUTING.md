# 贡献指南 / Contributing

## 开始之前

- 阅读 [项目范围](README.md#范围与许可)、[架构](docs/architecture.md) 和 [限制](docs/limitations.md)。
- 新增板卡、模型、算子或硬件配置前，先说明目标、验证方法和维护成本。
- 不提交训练数据、来源不明的模型、第三方源码副本或工具生成物。
- 不提交本地绝对路径、账号、设备序列号、邮箱或其他个人信息。

## 最低检查

```bash
make setup
make check
make soc-rtl
make gw5a-fw
```

涉及 FPGA 实现时还应运行：

```bash
make gw5a-bitstream
```

涉及板级行为时还应记录：

```bash
make gw5a-probe
make gw5a-detect-flash
SERIAL_PORT=ftdi://ftdi:2232h/2 make gw5a-demo-check
```

## 变更要求

- 地址映射修改必须同步 SpinalHDL、`sw/bsp/core/soc.h` 和 `docs/memory-map.md`。
- Flash 布局修改必须同步 boot stub、linker script、烧录脚本和 `docs/flash-layout.md`。
- UART 日志字段属于稳定接口；修改时同步 `scripts/monitor_serial.py` 和验证文档。
- RTL bridge 修改应增加协议级测试；板卡 pin 修改必须重新跑 implementation 和板测。
- 性能结论必须区分仿真、实现报告和物理板测，不用估算值替代实测值。

## 提交

- 一个提交处理一个可解释主题。
- subject 使用简短祈使句，例如 `Add GW5A Flash verification`。
- 提交前确认 `git status` 不包含 binary、日志、波形、实现目录或本地测试音频。
- PR 描述列出实际执行的命令和结果，不写“理论上通过”。

## English Summary

Keep changes focused, run the public, RTL, firmware, and relevant board checks, and never commit generated artifacts, local paths, device identifiers, datasets, or undocumented third-party material.
