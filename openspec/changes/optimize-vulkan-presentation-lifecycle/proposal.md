## Why

当前 Vulkan viewport 将 `render_finished` semaphore 绑定在 frame slot 上，并在 slot 的 submit fence 完成后复用。该 fence 只能证明 graphics submission 已完成，不能证明 WSI 已消费 present wait semaphore。当前 swapchain recreate 又在热路径调用 `vkDeviceWaitIdle()`，会在 resize、`OutOfDate` 和窗口交互时停止整个 device。

Toy3d 的目标是 VulkanPortable v1（Vulkan 1.1）正确可用，同时在驱动提供 WSI completion 能力时获得更低的重建停顿。UE4.27 的 per-image presentation semaphore 值得借鉴；其 `vkDeviceWaitIdle()` recreate 策略不应照搬。

## What Changes

- 将 GPU frame slot 与 swapchain image presentation state 分离：slot 只拥有 acquire semaphore、submit fence 和 command-pool payload；每个 swapchain image 独占一个 `render_finished` semaphore、image fence 和可选 present fence。
- Vulkan 1.1 正常帧只在再次 acquire 同一 image 后复用该 image 的 `render_finished` semaphore；submit fence 不再作为 WSI completion proof。
- 为 `vkQueuePresentKHR` 的成功、`Suboptimal`、`OutOfDate`、submit failure 和 abort 明确定义 semaphore 的复用、仅销毁或 terminal failure 语义。
- 将 swapchain 组织为 backend-private generation：新 generation 完整构造成功后才发布；旧 generation 按 graphics completion 与 presentation retirement 分阶段销毁。
- 无扩展的 recreate fallback 仅等待当前 graphics/present queue 的 `vkQueueWaitIdle()`，常规 resize/`OutOfDate` 路径禁止 `vkDeviceWaitIdle()`；device idle 仅保留在 shutdown 或不可恢复的终止路径。
- 可选启用 `VK_EXT_swapchain_maintenance1`：同时处理其 instance/device 依赖、feature query 和 device-create `pNext`，并以 present fence 加速旧 generation 的异步退休。
- 保持 backend-private 的 `max_frames_in_flight = 2`，实际 slot 数为 `min(2, actual_image_count)`。

本 change 不包含 acquire semaphore 首次使用 stage 的细化；该项作为后续独立性能 change，在有 profile 数据后推进。

## Capabilities

### New Capabilities

- `vulkan-presentation-lifecycle`: Vulkan viewport 内部 acquire、submit、present、同步对象退休与 swapchain generation 生命周期。

### Modified Capabilities

- 无。

## Impact

- 受影响代码：`engine/runtime/drivers/vulkan/vulkan_viewport_context.*`、`vulkan_queue.*`、`vulkan_device.*` 及对应 Vulkan 定向测试/CMake。
- `RHIViewportContext`、`RHIFrameEndResult` 和 D3D11/D3D12 公共 contract 不变；所有原生 Vulkan 同步与扩展状态保持在 backend 私有实现中。
- VulkanPortable v1 保留无扩展路径。`VK_EXT_swapchain_maintenance1` 仅为可选加速，不能成为 runtime 或 Cook 的必需能力。
