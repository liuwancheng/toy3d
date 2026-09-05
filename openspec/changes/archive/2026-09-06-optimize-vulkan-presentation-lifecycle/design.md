## Context

本 change 只修改 Render Thread 上的 Vulkan backend。公共 `RHIViewportContext` 继续隐藏 swapchain、image index、frame slot、semaphore 与 fence，submit 成功事实和 presentation status 仍通过 `RHIFrameEndResult` 分开表达。

当前实现的同步原则是正确的：frame slot 负责 CPU/GPU 周转，swapchain image 负责 WSI presentation，graphics submit fence 不能单独证明 WSI 已消费 present wait semaphore。问题在于该原则被表达为一组与 native owner 平行的 lifecycle、generation、publication 和 native API wrapper 类型，理解一次 present 需要同时跟踪多个对象及两份 per-image state。

UE4.27 的 `FVulkanViewport + FVulkanSwapChain` 提供了更直接的职责命名。本 change 借用该分层和 `ImageAcquired`/`RenderingDone` 术语，但不照搬 UE4.27 的 Fatal/check 错误模型、RHI Thread 历史路径或常规 recreate 的 device-wide idle。

VulkanPortable v1 基线仍为 Vulkan 1.1。第一阶段只使用一个同时支持 graphics 与 present 的 queue family；timeline semaphore、Synchronization2、present wait、独立 present queue、async compute 和 pass 并行均不在本 change 范围内。

## Goals / Non-Goals

**Goals:**

- 用稳定的 owner 类型直接表达 viewport、swapchain、swapchain image 和 frame slot 的职责。
- 保留 per-image rendering-done semaphore、frame-slot completion 和 submit/present 错误边界。
- 删除只为机制或测试形状存在、且没有形成完整验证闭环的 wrapper、tracker 和镜像状态。
- 正常帧不等待 queue/device idle；recreate 只等待当前 shared graphics/present queue。
- 保持最多两个 frame slots，且不把 swapchain image 数量等同于 CPU frame-ahead 深度。

**Non-Goals:**

- 不改变公共 RHI viewport、queue completion、RenderScene 或 D3D11/D3D12 contract。
- 不保留或重新包装 `VK_EXT_swapchain_maintenance1`、present fence 和异步 retired-generation 机制。
- 不新增通用 `VulkanNativeApi`、测试专用公共接口、RHI thread、render graph 或多 queue ownership transfer。
- 不优化 acquire semaphore 的 pipeline wait stage，也不实现 delayed acquire。

## Type And Naming Decisions

以下名称是本 change 的正式 backend-private type contract：

| 类型 | 状态 | 稳定职责 |
| --- | --- | --- |
| `VulkanViewportContext` | 保留 | 实现公共 `RHIViewportContext`；拥有 active frame、frame-slot 轮转、业务 submit、abort、resize pending 和 terminal status |
| `VulkanSwapchain` | 新增 | 拥有一个 native `VkSwapchainKHR` 及其 image/view/RHI wrapper；执行 image acquire、present 和完整构造/销毁 |
| `VulkanSwapchainImage` | 新增，backend-private | 表达一张稳定 swapchain image 的 native/RHI 身份、per-image `rendering_done` semaphore 与上次 graphics 使用 fence |
| `VulkanFrameSlot` | 由 `FrameSlot` 重命名，backend-private | 表达 CPU/GPU frame-ahead 周转域；拥有 `image_acquired` semaphore、`submission_fence`、command pool 和提交 payload |

`VulkanSwapchainImage::last_submission_fence` 是 non-owning handle，只指向当前 viewport 中某个 `VulkanFrameSlot::submission_fence`；swapchain/frame slots 的销毁前必须先完成 shared-queue drain。`VulkanSwapchainImage` 不拥有该 fence，也不得销毁它。

删除以下不再表达独立长期职责的名称：

| 删除名称 | 收敛位置 |
| --- | --- |
| `SwapchainGeneration` | 一个 `VulkanSwapchain` 实例天然代表一代 native swapchain |
| `SwapchainImagePresentationState`、`VulkanImageLifecycleState` | 合并为 `VulkanSwapchainImage` |
| `VulkanImagePresentationPhase` | 由成功路径、recreate pending 和 terminal 控制流表达 |
| `VulkanPresentTransition` | `VulkanSwapchain::present()` 直接返回 `RHIStatus` |
| `VulkanGenerationRetirementMode` | recreate 固定使用 shared-queue idle，无运行时退休策略选择 |
| `VulkanGenerationLifecycle` | image ownership 与 present 结果处理收回 `VulkanSwapchain`/`VulkanViewportContext` |
| `VulkanGenerationPublicationTracker` | observation 直接读取实际 owner，历史计数作为 viewport 普通字段 |
| `VulkanPresentationNativeApi`、`VulkanPresentationNativeApiDefault` | 职责 owner 直接调用 Vulkan API |

成员命名采用 UE4.27 易识别术语并遵守 Toy3d snake_case：

| 当前成员 | 新成员 |
| --- | --- |
| `image_available` | `image_acquired` |
| `completion_fence` | `submission_fence` |
| `render_finished` | `rendering_done` |
| `active_generation` | `swapchain` |
| `current_frame_slot` | 保留 |
| `active_image_index` | 保留 |

## Ownership

```text
VulkanDevice
└─ VulkanQueue

VulkanViewportContext
├─ unique_ptr<VulkanSwapchain> swapchain
├─ vector<VulkanFrameSlot> frame_slots
├─ current_frame_slot
├─ active_image_index
├─ resize_pending
└─ presentation_failure

VulkanSwapchain
├─ VkSwapchainKHR
├─ VkFormat / VkExtent2D
└─ vector<VulkanSwapchainImage>
   ├─ VkImage / VkImageView
   ├─ RHITextureRef / RHITextureViewRef
   ├─ VkSemaphore rendering_done
   └─ VkFence last_submission_fence (non-owning)
```

`VulkanViewportContext` 是 frame orchestration owner；`VulkanSwapchain` 是 WSI object owner；`VulkanQueue` 是 native submit ordering 和 completion owner。三者不得通过新的 manager 或 global singleton 复制彼此状态。

## Normal Frame

`begin_frame()`：

1. 若已锁存 terminal failure，直接返回原始诊断。
2. 若 `resize_pending` 或尚无 swapchain，在干净边界执行同步 recreate。
3. 等待当前 `VulkanFrameSlot::submission_fence`，释放该 slot 的 command lists/resources/uploads/descriptors，reset command pool。
4. 调用 `VulkanSwapchain::acquire_image(frame_slot.image_acquired)`。
5. 若 acquired image 的 `last_submission_fence` 指向另一 slot 且尚未完成，等待该 fence。
6. 返回只暴露公共 texture/view 和 frame-local command context 的 `RHIFrameContext`。

`end_frame()`：

1. 在 native submit 前验证 frame、device、viewport、command-list identity/state 和 local resource state。
2. submit 业务 command buffer 与 backend present-transition command buffer；等待 `image_acquired`，signal acquired image 的 `rendering_done`，并使用当前 slot 的 `submission_fence`。
3. submit 成功后设置 image 的 non-owning `last_submission_fence`，保留 GPU payload 并发布 queue committed state。
4. `VulkanSwapchain::present()` 等待 acquired image 的 `rendering_done`。
5. 返回有效 completion value 和独立 presentation status；present 失败不得回滚已成功的业务 submit。

同一 image 的 `rendering_done` 只在该 image 后续再次成功 acquire 后复用。不同 image 的 semaphore 和上次提交 fence 独立维护；frame slot index、image index、逻辑 frame id 和 queue completion value 不得互换。

## Synchronous Swapchain Recreation

recreate 仅在没有 active acquired frame 的 `begin_frame()` 边界执行：

1. 查询 surface capabilities；zero extent 返回 `NotReady`，不销毁当前 owner。
2. 等待当前 shared graphics/present queue idle。等待失败时保留现有 owner 供 terminal cleanup，并锁存原始 terminal 诊断。
3. 在局部 `std::unique_ptr<VulkanSwapchain>` 中创建 replacement；factory 必须以 RAII 回收部分创建的 native objects。
4. replacement 的 swapchain、images/views、RHI wrappers 和 per-image semaphores 全部成功后才发布。
5. 销毁旧 `VulkanSwapchain`，按 `min(max_frames_in_flight, actual_image_count)` 重建 `VulkanFrameSlot`，再清除 `resize_pending`。

常规 recreate 禁止调用 `vkDeviceWaitIdle()`。当前 shared-queue 假设写入 backend contract；未来支持独立 present queue 时，必须重新设计为等待所有使用旧 swapchain 的 graphics/present queues，不能沿用本路径。

该同步方案只可能在 resize、`Suboptimal` 或 `OutOfDate` 后停顿，正常 frame 不增加 idle wait。`VK_EXT_swapchain_maintenance1`、present fence、`retired_generations` 和 deferred present fence 全部删除；若真实性能数据证明 live resize stall 不可接受，再用独立 change 恢复异步 retirement。

## Failure Model

| 边界 | 行为 |
| --- | --- |
| acquire `OutOfDate` | 不产生 frame，保持 `resize_pending`，后续干净边界 recreate |
| acquire `Suboptimal` | 本帧可继续，设置 `resize_pending` |
| submit 失败 | 业务未成功提交；acquired synchronization 状态不可继续复用，锁存 terminal |
| submit 成功、present `Suboptimal`/`OutOfDate` | 外层 frame result 成功并保留 completion；先 commit 业务状态，再设置 `resize_pending` |
| submit 成功、present terminal | 外层仍表达业务已提交；先 commit，再进入 terminal |
| recording 失败 | `abort_frame()` 以最小 transition/submit/present 闭合 acquire；闭环失败即 terminal |
| recreate queue wait 或构造失败 | 不发布半初始化 replacement；保留原始诊断并按 recoverable/terminal 分类处理 |

## Native API And Tests

`VulkanPresentationNativeApi` 删除。`VulkanQueue` 直接调用 `vkQueueSubmit`/`vkQueueWaitIdle`，`VulkanSwapchain` 直接调用 swapchain/acquire/present 和其对象创建销毁 API，`VulkanViewportContext` 只直接调用 frame-slot fence/command-pool API。

测试按行为边界组织：

- 纯结果映射测试覆盖 `VK_SUCCESS`、`VK_SUBOPTIMAL_KHR`、`VK_ERROR_OUT_OF_DATE_KHR`、`VK_ERROR_DEVICE_LOST` 和内存错误，不为此新增 public/backend virtual interface。
- frame-slot/image 交错测试验证两个 slots 与三个 images 不混用 identity，且 `last_submission_fence` 只作为 non-owning 上次 graphics 使用记录。
- viewport/public fake 覆盖 submit/present 分离和 abort normalization。
- 真实 Vulkan validation smoke 覆盖正常多帧、resize、minimize/restore、连续 out-of-date 和 shutdown。

如果未来需要创建失败或驱动返回值的完整故障注入，应先证明纯函数和真实 smoke 无法覆盖，再设计能驱动真实 `VulkanSwapchain`/`VulkanViewportContext` 的 backend-wide dispatch；禁止重新引入只验证 mock 自身的部分 wrapper。

## Risks / Trade-offs

- resize 时 shared queue idle 可能造成短暂停顿；这是方案 A 为降低第一阶段实现复杂度接受的明确代价，正常帧不受影响。
- 删除 maintenance1 会失去异步旧 swapchain retirement；恢复条件必须是真实 profile/用户体验证据，而不是扩展可用性本身。
- direct Vulkan calls 降低细粒度错误注入能力；以结果映射测试、公共 fake 和真实 validation smoke 组合覆盖，避免为测试泄漏机制性接口。
- `VulkanSwapchainImage::last_submission_fence` 是 non-owning；销毁顺序和 queue drain 必须由注释、断言和测试共同固定。

## Migration Plan

1. 先更新 Active RHI/Vulkan 内存文档，登记方案 A、正式类型名、shared-queue recreate 和 maintenance1 删除边界。
2. 新增 `VulkanSwapchain`/`VulkanSwapchainImage`，迁移 native swapchain、image/view、per-image semaphore 与 acquire/present 实现。
3. 将 `FrameSlot` 重命名为 `VulkanFrameSlot`，迁移 `image_acquired`、`submission_fence` 和 payload ownership；保持公共 frame API 不变。
4. 将 recreate 改为干净边界 shared-queue idle + replacement publication，删除 retired generations、present fences 和 lifecycle/tracker 状态。
5. 删除 `VulkanPresentationNativeApi`，让 queue/swapchain/viewport 各自直接调用职责内 Vulkan API，并移除 device 中的 wrapper ownership/accessor。
6. 删除 `vulkan_presentation_lifecycle.*` 和旧 mock/tracker 测试，建立新的 swapchain、viewport status 与真实 Vulkan validation 覆盖。
