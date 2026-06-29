# 限制 / Limitations

- v0.1 只支持 Sipeed Tang Primer 25K / GW5A-25A、50 MHz 和当前 pin constraints。
- 支持 12 类英文 KWS：`one` 到 `eight`、`up`、`down`、`noise`、`silence`。
- 不分发训练数据、训练脚本、源 ONNX 模型或音频测试文件。
- 不提交 bitstream、ELF、bin、波形、综合报告或 implementation directory。
- UART 是 bring-up 和结果接口，不是高带宽实时音频通道。
- 推理期间固件会清空并丢弃麦克风 FIFO 数据，完成后重新监听；当前版本不支持重叠推理。
- 当前资源中 CLS 和 BSRAM 已接近 90%，新增 cache、NPU cluster 或大 FIFO 需要重新评估容量。
- 当前 testvector 的旧 behavioral/plan simulator top1 为 7，而 NPU RTL 与 GW5A 实测 golden 均为 5。硬件回归使用独立的 `VC_KWS_EXPECTED_TOP1_RTL=5`；编译器数值模型与 RTL 的差异尚未作为 v0.1 的训练/编译流程解决。
- 设计不提供 secure boot、模型认证、权限隔离、冗余或故障安全机制，不适合量产和安全关键用途。

## English Summary

Version 0.1 targets one Sipeed Tang Primer 25K (GW5A-25A) configuration and a fixed 12-class English KWS demo. The legacy behavioral golden differs from the RTL/board golden for the bundled test vector. This is a research prototype, not a production or safety-critical platform.
