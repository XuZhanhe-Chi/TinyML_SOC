# 安全策略 / Security Policy

## 支持范围

仅最新发布版本接收安全修复。TinyML_SOC 是研究和教学原型，未针对安全关键、功能安全或量产环境进行审计。

## 报告漏洞

使用仓库的 GitHub Private Vulnerability Reporting 或 Security Advisory 私下报告。请提供受影响版本、复现步骤、影响和建议修复；在维护者完成初步评估前不要公开利用细节。

以下问题属于本项目范围：

- host 构建/烧录脚本的命令或路径注入；
- 固件和 DMA 的越界访问；
- 不可信 bundle、uOP 或 Flash image 导致的内存破坏；
- 公开仓库意外包含凭据或个人信息。

## 部署边界

当前 demo 假设 firmware、bundle、bitstream、JTAG host 和物理环境可信。工程不实现 secure boot、bitstream/model authentication、权限隔离、加密存储或对抗样本防护。

## English Summary

Report vulnerabilities privately. This research prototype assumes trusted firmware, models, bitstreams, JTAG access, and physical hardware, and does not implement secure boot or isolation.
