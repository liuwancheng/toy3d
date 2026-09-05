## Why

当前 Vulkan presentation 已正确区分 frame slot、swapchain image、graphics completion 与 WSI completion，但实现把扩展 ABI、native-call 测试 seam、generation 状态机和观测计数集中在 `vulkan_presentation_lifecycle.*`。`VulkanGenerationLifecycle`、`VulkanGenerationPublicationTracker` 与 viewport 中的 native image state 形成两套需要按 image index 同步的状态；`VulkanPresentationNativeApi` 又只包装部分 Vulkan 调用，名称与其 queue submit、command-pool 创建等职责不一致，现有测试主要验证 mock 自身而未驱动真实 viewport orchestration。

Toy3d 第一阶段固定为 VulkanPortable v1、单 graphics queue 且 graphics/present 使用同一 queue family。此阶段更适合采用 UE4.27 容易识别的 `Viewport + SwapChain` 职责边界：正常帧继续使用 per-image rendering-done semaphore，swapchain recreate 只在干净边界同步等待 shared queue idle。该方案保留同步正确性，同时删除尚无真实性能证据支撑的 `VK_EXT_swapchain_maintenance1` 异步 retirement 复杂度。

## What Changes

- 新增 backend-private `VulkanSwapchain`，集中拥有 `VkSwapchainKHR`、surface format/extent 与 `VulkanSwapchainImage` 集合，并提供 `acquire_image()`、`present()` 和 presentation texture/view 查询。
- `VulkanSwapchainImage` 统一保存 native image/view、公共 RHI texture/view、per-image `rendering_done` semaphore 与 non-owning `last_submission_fence`，删除 native state 与 lifecycle state 的双份 image 记录。
- `VulkanViewportContext` 保持公共 frame owner；其 backend-private `VulkanFrameSlot` 只拥有 `image_acquired` semaphore、`submission_fence`、command pool、present-transition command buffer、completion value 和本次提交 payload。
- swapchain resize、`Suboptimal` 或 `OutOfDate` 在后续 `begin_frame()` 干净边界执行同步 recreate：等待当前 shared graphics/present queue idle，完整创建 replacement，成功后发布并销毁旧 swapchain；常规路径禁止 `vkDeviceWaitIdle()`。
- 删除 `VulkanGenerationLifecycle`、`VulkanGenerationPublicationTracker`、`VulkanPresentTransition`、`VulkanGenerationRetirementMode`、retired-generation/present-fence 状态和 `VK_EXT_swapchain_maintenance1` capability gate。
- 删除不完整的 `VulkanPresentationNativeApi`/`VulkanPresentationNativeApiDefault`。Vulkan queue、swapchain 与 viewport owner 直接调用职责内的 Vulkan API；若未来确需完整 native failure injection，另行设计 backend-wide 且能驱动真实 orchestration 的统一 dispatch。
- 保留公共 `RHIViewportContext`、`RHIFrameEndResult`、submit/present 分离、`abort_frame()` 最小闭环、per-image semaphore 和最多两个 frame slots 的 contract。
- acquire semaphore 首次使用 stage 优化仍不在本 change 范围内；当前保守同步待真实性能数据后单独处理。

## Capabilities

### New Capabilities

- `vulkan-presentation-lifecycle`: Vulkan backend 以 `VulkanViewportContext + VulkanSwapchain` 管理 acquire、submit、present、同步 recreate 与 frame/image 生命周期。

### Modified Capabilities

- 无。

## Impact

- 受影响代码：`engine/runtime/drivers/vulkan/vulkan_viewport_context.*`、新增 `vulkan_swapchain.*`、`vulkan_queue.*`、`vulkan_device.*`、Vulkan runtime CMake 与对应测试。
- 删除 `engine/runtime/drivers/vulkan/vulkan_presentation_lifecycle.*` 及其旧定向测试入口。
- `RHIViewportContext`、`RHIFrameEndResult`、RenderScene 调用链和未来 D3D11/D3D12 公共 contract 不变；全部 Vulkan native 类型继续留在 backend。
- 正常帧不增加 queue/device idle；resize/recreate 允许一次 shared-queue stall，以换取显著更简单、可审计的所有权模型。
