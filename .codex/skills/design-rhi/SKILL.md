---
name: design-rhi
description: 设计、实现和审查 Toy3d 的跨图形 API RHI。新增或修改公共 RHI 接口、实现 Vulkan 后端、检查 renderscene 与 RHI 边界，或评估 Vulkan、DirectX 10、DirectX 12 可实现性时使用。
---

# Toy3d RHI 设计、实现与审查

## 目标

设计并实现小而清晰、可由 Vulkan、DirectX 10 和 DirectX 12 实现的 RHI。只在本文件规定工作方法；分别维护项目需求、外部参考和当前代码审查结论。

## 资料路由

- 设计、实现或修改公共 RHI：完整阅读 `references/toy3d-rhi-requirements.md`。
- 需要解释架构来源或核对 UE4.27 做法：再读 `references/ue427-rhi-summary.md`。
- 实现 Vulkan 后端、审查或迁移当前代码：再读 `references/toy3d-rhi-current-review.md`，并检查公共 RHI、Vulkan 后端和 renderscene 调用方的完整调用链。
- UE 源码默认位于 `D:/ue4.27plus/Engine/Source/Runtime/`；仅在精简参考未覆盖关键细节时读取，并区分“UE 参考事实”和“Toy3d 选择”。

## 工作流

1. 从上层需要表达的操作、资源、生命周期和同步关系出发，不从某个后端函数签名反推公共接口。
2. 确定所属层：公共 descriptor、device 创建、command context 执行、queue/swapchain 提交、上层 pass 调度或后端原生实现。
3. 对每项语义评估 Vulkan、D3D10、D3D12 的等价实现、安全降级和明确不支持路径。
4. 写清资源创建者、CPU 所有者、GPU in-flight 引用、销毁者、线程归属及失败行为。
5. 实现时先收敛公共语义和 validation，再修改 backend hook、原生实现及调用方；未实现路径返回 `Unsupported` 或可诊断错误。
6. 输出或复查需求映射、接口、三后端映射、生命周期与同步、错误模型、迁移步骤和未决问题。
7. 将新的产品边界写入需求文档，仅将当前代码问题写入审查记录，只将 UE 事实写入 UE 参考。
8. 检查公共头文件无后端类型、枚举转换集中在后端、失败可诊断；代码改动完成后交由 sub-agent 使用 `verify-toy3d-build` 独立验证，纯文档改动无需构建。

## 审查输出

- 先列违反已确认需求的问题，再列建议和未决项。
- 每个问题包含文件位置、影响、三后端可实现性及建议边界。
- 严重度使用 P0（正确性或闭环阻塞）、P1（接口定型前解决）、P2（可后置清理）。
- 使用中文说明，代码标识符和 API 名称保持原文。
