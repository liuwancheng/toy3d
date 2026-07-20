# Toy3d RHI 当前实现审查

## 文档职责

本文记录截至 2026-07-20 的代码现状问题，是可更新的迁移清单，不是长期需求。旧 `IDynamicRHI` 原型与 legacy Vulkan 实现已经删除，当前 Vulkan 后端直接实现 `RHIDevice` 管线。

## P0

当前没有未完成项。

## 已完成

1. 已删除公共 `RHISwapchain`、`RHIDevice::create_swapchain()` 以及未完成的 `VulkanSwapchain` 平行路径。presentation 唯一公共入口为 `RHIViewportContext`，Vulkan 的 swapchain、acquire、submit、present 和 resize 继续由 `VulkanViewportContext` 内部管理。
2. Vulkan command list 已使用 local buffer/texture state tracker；录制 transition、copy、upload、render pass 和 binding 不再提前修改 committed state。queue/viewport 在原生提交前验证 initial state，并仅在 `vkQueueSubmit` 成功后提交 final state；丢弃、录制失败和提交失败不会污染资源状态。viewport 的隐式 present barrier 读取 command list 的最终局部 layout。
3. 公共 `RHIViewportContext::abort_frame()` 已明确消费 acquire 后无法继续录制的失败帧。Vulkan 通过最小提交消费 acquire semaphore、恢复并提交 backbuffer `Present` 状态、尝试 present，并推进 frame slot；renderscene 失败路径不再使用空 command-list 的 `end_frame()` 隐式收尾。
4. Vulkan frame slot 在提交成功后直接强持有完整 command list，直到 completion fence 后才释放，以 command list 作为所有录制期 GPU payload 的统一生命周期根。pipeline descriptor 传递持有 shader 和 binding layout，binding set descriptor 传递持有 layout、buffer、texture view 和 sampler，binding set 自身持有 descriptor pool/set；upload page 另按 completion value 退休。Buffer View 当前明确返回 `Unsupported`，不存在未保活的已录制路径。
5. 公共 `RHICPUAccess` 已收紧为 `None`、`Read`、`Write` 三种访问需求并移除无调用方的 `ReadWrite`。该枚举不选择 native heap/memory type，也不承诺 persistent mapping；Vulkan 在 readback/map 闭环实现前对 CPU-accessible resource 明确返回 `Unsupported`。
6. texture transition 已支持 `RHISubresourceRange` 的 aspect、mip 与 array layer 精确范围。Vulkan texture committed state 按 subresource 保存，command list 以稀疏 delta 记录 initial/final state，barrier 使用精确 `VkImageSubresourceRange`；copy、upload、render-pass attachment 与 sampled binding 均按实际访问范围验证，混合状态范围明确报错，提交失败仍不发布 final state。

## P1

1. CPU map/unmap 尚未定义统一 lock mode、range、alignment、flush/invalidate及in-flight冲突；应与 GPU fence/readback 能力一并定型，禁止资源对象私自 submit 或 wait idle。
2. `RHIDevice::create_graphics_command_context()` 与 `RHIFrameContext::create_graphics_command_context()` 并存，Vulkan前者返回 `Unsupported`。需要明确非frame录制的产品需求；若第一阶段只允许frame-local context，应从公共device主路径移除或后置。
3. `create_buffer/create_texture(initial_data)` 在Vulkan明确返回`Unsupported`，当前上传只能通过frame-local context完成；需要保持诊断行为并决定后续初始化批次，不得引入隐式submit/wait idle。
4. Buffer View、storage binding、resolve attachment和GPU fence/readback尚未闭环；其 capability和错误路径需要与Vulkan、D3D10、D3D12映射一起定型。
5. 正式RDG尚未实现。后续直接在renderscene建设RDG，不新增临时Pass Scheduler；在RDG接管跨pass状态前，手写renderscene transition只能作为RHI bring-up代码。

## P2

1. 公共枚举仍包含legacy spelling alias；完成调用方迁移后应删除，避免cache key、日志和后端转换存在双重名称。
2. 当前公共行为命名混用`Desc`、`Info`、`set_graphics_pipeline`、`transition_resources`等风格；按UE语义和Toy3d命名规则分调用链迁移，不做一次性无关重命名。
3. shader reflection、binding参数模型和完整pipeline cache仍需成熟；最终公共命名不能泄漏descriptor set/root signature概念。
