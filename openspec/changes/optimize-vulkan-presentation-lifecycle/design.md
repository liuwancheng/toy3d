## Context

本 change 只修改 Render Thread 上的 Vulkan viewport backend。公共 `RHIViewportContext` 继续隐藏 swapchain、image index、frame slot、semaphore 与 fence。

当前实现中一个 `FrameSlot` 同时拥有 acquire semaphore、`render_finished` semaphore、submit fence 和 command pool。slot fence 可以退休 command pool、acquire semaphore 与 GPU payload，但不能证明 WSI 已消费 `render_finished`。`recreate_swapchain()` 目前直接调用 `vkDeviceWaitIdle()`。

VulkanPortable v1 固定 Vulkan 1.1，只有一个同时支持 graphics 与 present 的 queue family。timeline semaphore、Synchronization2 和任何 WSI extension 都不能成为基线条件。

## Goals / Non-Goals

**Goals:**

- 正确区分 graphics completion、WSI presentation completion、frame slot 与 swapchain image 的生命周期。
- 正常帧和常规 resize/`OutOfDate` recreate 不调用 `vkDeviceWaitIdle()`。
- 用有限且可审计的同步对象实现 Vulkan 1.1 fallback；在支持时利用 present fence 异步退休旧 generation。
- 将 CPU ahead 限制为最多两个 frame slot，同时不把 surface image 数量等同于 CPU in-flight 深度。

**Non-Goals:**

- 不改变任何公共 RHI viewport、completion value 或 D3D11/D3D12 contract。
- 不引入 timeline semaphore、Synchronization2、present wait、async compute、多 queue ownership transfer、RHI thread 或 render graph。
- 不实现 `VK_EXT_swapchain_maintenance1` 的 deferred memory allocation、release images、present mode switching 或 scaling 功能。
- 不在本 change 优化 acquire semaphore 的 pipeline wait stage；当前保守 stage 保持不变。

## Decisions

### 1. Frame slot 与 image presentation state 分离

每个 `FrameSlot` 只拥有 `image_available` acquire semaphore、`completion_fence`、completion value、command pool、present transition command buffer，以及与 submit completion 绑定的 command-list、resource、upload 与 descriptor 强引用。

每个 `SwapchainImagePresentationState` 隶属于一个 swapchain generation，只拥有一个长期存在的 `render_finished` semaphore、`image_fence`，以及可选的 `present_fence` 和 presentation state。

同一 image 在被再次 acquire 前不能再次提交 present，因此一个 image 一个 `render_finished` semaphore 已经足够。禁止将 submit completion fence 当作该 semaphore 可复用的依据。

### 2. 明确 present 结果状态机

| 事件 | semaphore / image 状态 |
| --- | --- |
| submit 成功，`vkQueuePresentKHR` 返回 `VK_SUCCESS` 或 `VK_SUBOPTIMAL_KHR` | WSI 持有 `render_finished`；无扩展时等待同 image 的下一次成功 acquire，扩展路径等待 present fence。 |
| submit 成功，`vkQueuePresentKHR` 返回 `VK_ERROR_OUT_OF_DATE_KHR` 或不可恢复错误 | 不假定 WSI 已消费 `render_finished`；该 semaphore 只能在对应 submit fence 完成后销毁，禁止复用。不可恢复错误锁存为 terminal failure。 |
| submit 失败 | acquire semaphore、fence 或 acquired image 的状态不能安全重试；锁存 terminal failure 并禁止复用不确定对象。 |
| abort frame | 提交最小的 transition/present 闭环以消费 acquire synchronization；若该闭环任一步失败，锁存 terminal failure。 |

`VK_SUBOPTIMAL_KHR` 表示本帧 present 成功且下一帧应重建；它不改变 WSI wait semaphore 的退休规则。

### 3. Extension capability gate 完全封装在 Vulkan backend

`VK_EXT_swapchain_maintenance1` 不是 core Vulkan feature。快路径仅在以下全部满足时启用：

1. instance 启用 `VK_KHR_get_surface_capabilities2` 与 `VK_EXT_surface_maintenance1`；
2. device 启用 `VK_KHR_swapchain` 与 `VK_EXT_swapchain_maintenance1`；
3. Vulkan 1.1 基线满足 properties2 的 core 依赖；
4. `vkGetPhysicalDeviceFeatures2` 查询到 `VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT::swapchainMaintenance1`，并在 `VkDeviceCreateInfo::pNext` 中显式启用它。

能力枚举、instance/device 创建和 feature 链全部位于 `VulkanDevice`；viewport 只读取 backend-private capability。启用失败或未支持时必须无痕回退 Vulkan 1.1 路径。

快路径为每次已成功提交给 WSI 的 present 关联 `VkSwapchainPresentFenceInfoEXT` 中的 fence；只有该 fence signal 后才能异步退休相应 generation 的 presentation objects。

### 4. Transactional generation create 与 retirement

`VulkanSwapchainGeneration` 是 viewport-private 对象，含 native swapchain、images/views、presentation states、frame slots 与仍在 WSI 中的 present 记录。

创建新 generation 时先在临时对象中完成所有 native object 与 RHI wrapper 的构造，成功后才替换 active generation。创建失败时保留旧 generation 的可诊断状态和安全回收路径，禁止发布半初始化对象。

新 generation 将旧 handle 传给 `VkSwapchainCreateInfoKHR::oldSwapchain`。旧 generation 的 graphics-local payload 按 submit completion 退休；其 image view、semaphore 与 native swapchain 只在所有相关 presentation state 达到安全退休条件后销毁。

无 extension 时，recreate 后不能再通过 acquire 旧 generation 的 image 获得 WSI proof。因此只在即将销毁该旧 generation 时调用当前 shared graphics/present queue 的 `VulkanQueue::wait_idle()`。这是一条有界、单 queue 的 fallback，禁止改写为 `vkDeviceWaitIdle()`。未来引入独立 present queue 前，必须扩展为等待全部曾向旧 generation 提交 graphics/present 的队列，不能隐式沿用本假设。

`vkDeviceWaitIdle()` 仅允许用于 shutdown、device loss 或已锁存 terminal failure 后的最终资源清理。

### 5. 独立 frame-in-flight 上界

generation 创建时分配 `slot_count = min(max_frames_in_flight, actual_image_count)`，其中 `max_frames_in_flight` 是 viewport-private 常量 `2`。image presentation state 始终按实际 image identity 管理；不能因 surface 返回三个 image 而允许三个 CPU frame ahead。

### 6. 观测与测试

viewport observation 至少报告 active/retired generation 数、slot 数、每 image presentation state、pending present fence 数、fallback queue drain 次数、不可复用 semaphore 销毁次数和 extension-enabled 状态。Vulkan 定向测试通过 backend-private native-call seam 覆盖状态机，不要求真实窗口；真实 Vulkan smoke 覆盖 validation layer 与 resize。

## Risks / Trade-offs

- extension 依赖横跨 instance、device 与 feature chain：以单一 backend-private capability gate 统一判断，避免 viewport 自行探测。
- 无扩展 resize 仍可能有一次 queue-level stall：它只作用于当前 graphics/present queue，且仅在 retirement 边界发生；正常 frame 无 idle。
- present 失败的 WSI 是否消费 semaphore 不可由 submit fence 推断：失败状态表强制仅销毁或 terminal failure。
- 两个 slot 在某些吞吐场景会少于 image-count slot：先以交互延迟为默认，后续依据 profile 决定是否开放 backend policy。

## Migration Plan

1. 在设计文档与定向测试中建立 per-image state 和 present-result 真值表。
2. 将 FrameSlot 的 `render_finished` 迁移到每 image state，接入同 image reacquire retirement 和失败只销毁路径。
3. 引入 transactional generation，对无扩展 recreate 使用 `VulkanQueue::wait_idle()` 退休旧 generation，删除正常路径的 device idle。
4. 在 `VulkanDevice` 接入完整 extension/feature capability gate；在可用设备上加入 present-fence retirement。
5. 在 validation layer、resize/minimize/restore、extension enabled/disabled 和 shutdown 上完成验证；acquire wait-stage 优化留给后续独立 change。
