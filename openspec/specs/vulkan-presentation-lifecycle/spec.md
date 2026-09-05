# vulkan-presentation-lifecycle Specification

## Purpose
定义 Vulkan backend 以 UE4.27 易识别的 `Viewport + SwapChain` 分层管理 acquire、submit、present、frame/image 同步和同步 swapchain recreate，并保持公共 RHI presentation 边界不泄漏 native 对象。

## Requirements

### Requirement: Viewport 与 Swapchain 职责分层

Vulkan backend SHALL 使用 `VulkanViewportContext + VulkanSwapchain` 作为 presentation 的唯一 owner 链。`VulkanViewportContext` SHALL 管理公共 frame 生命周期与 frame slots；`VulkanSwapchain` SHALL 管理 native swapchain、swapchain images、per-image presentation semaphore、acquire 和 present。backend MUST NOT 再建立与 owner 平行的 generation lifecycle、publication tracker 或 presentation manager。

#### Scenario: 上层开始一帧

- **WHEN** RenderScene 调用公共 `RHIViewportContext::begin_frame()`
- **THEN** `VulkanViewportContext` MUST 通过其 active `VulkanSwapchain` acquire image，并只向上层返回公共 texture/view 与 frame-local command context

#### Scenario: Swapchain 对象完整销毁

- **WHEN** 一个 `VulkanSwapchain` 被销毁
- **THEN** 它 MUST 释放自己拥有的 image views、RHI wrappers、per-image `rendering_done` semaphores 与 `VkSwapchainKHR`，且不得释放 non-owning `last_submission_fence`

### Requirement: FrameSlot 与 SwapchainImage 独立

`VulkanFrameSlot` SHALL 表达 CPU/GPU frame-ahead 周转域，`VulkanSwapchainImage` SHALL 表达 WSI image identity；slot index、image index、logical frame id 和 queue completion value MUST NOT 互换。slot 数 MUST 为 `min(2, actual_image_count)`，且 swapchain image 数量增大不得自动提高 CPU frame-ahead 深度。

#### Scenario: 三张 image 与两个 slots 交错

- **WHEN** surface 提供三张 images，而连续 frame 使用两个 slots acquire 到不同 image 顺序
- **THEN** 每个 slot MUST 只回收自己的 command-pool/payload，每张 image MUST 独立维护自己的 `rendering_done` 和 `last_submission_fence`

### Requirement: Per-image rendering-done semaphore

每个 `VulkanSwapchainImage` SHALL 独占一个 `rendering_done` semaphore。graphics submit completion MUST NOT 单独作为 WSI 已消费该 semaphore 的证明；正常运行中，只有同一 swapchain image 后续再次成功 acquire 后，backend 才可复用该 image 的 `rendering_done`。

#### Scenario: Submit fence 先完成

- **WHEN** image `i` 的 graphics submission fence 已 signal，但 image `i` 尚未再次 acquire
- **THEN** backend MUST NOT 将该 fence 单独解释为 WSI completion，也不得把 image `i` 的 `rendering_done` 用于另一张 image

#### Scenario: 同一 image 再次 acquire

- **WHEN** `VulkanSwapchain::acquire_image()` 再次成功返回 image `i`
- **THEN** backend MAY 在本次 image `i` submission 中复用其 `rendering_done`，且其他 image 的 semaphore 不受影响

### Requirement: 同步 Swapchain Recreate

resize、`Suboptimal` 或 `OutOfDate` 后的 swapchain recreate SHALL 只在没有 active acquired frame 的后续 `begin_frame()` 干净边界执行。当前单一 shared graphics/present queue 基线下，backend MUST 在替换或销毁旧 presentation 对象前调用该 queue 的 idle wait，MUST NOT 在常规 recreate 调用 `vkDeviceWaitIdle()`。

`VulkanViewportContext` MUST 在局部 owner 中完整创建 replacement `VulkanSwapchain`，只有 native swapchain、images/views、RHI wrappers 和 per-image semaphores 全部成功后才发布；部分构造失败 MUST 由 RAII 清理且不得发布半初始化 replacement。同步 recreate MUST NOT 维护 `retired_generations`、present fences 或异步 generation retirement。

#### Scenario: Surface extent 为零

- **WHEN** resize/minimize 后 surface extent 为零
- **THEN** `begin_frame()` MUST 返回 `NotReady`，保持 recreate pending，且不得销毁当前 presentation owner

#### Scenario: Replacement 创建成功

- **WHEN** shared queue idle 成功且 replacement `VulkanSwapchain` 完整创建
- **THEN** viewport MUST 原子发布 replacement、销毁旧 swapchain、按实际 image count 重建最多两个 frame slots，并清除 recreate pending

#### Scenario: Queue idle 失败

- **WHEN** recreate 的 shared-queue idle wait 失败
- **THEN** viewport MUST 保留旧 owner 供 terminal cleanup、锁存原始 terminal 诊断，并禁止继续 acquire 或复用旧同步对象

#### Scenario: Replacement 构造失败

- **WHEN** replacement 的任一 native object 或 RHI wrapper 创建失败
- **THEN** viewport MUST NOT 发布 replacement，MUST 清理部分新对象，并按原始 recoverable/terminal status 决定保持 pending 或停止后续 frame

### Requirement: Submit 与 Present 结果边界

Vulkan viewport MUST 在 native submit 前完成 frame、device、command-list 与 resource-state validation。submit 成功后 SHALL 设置 acquired `VulkanSwapchainImage::last_submission_fence`、保留本次 payload 并发布 queue committed state；后续 present 的任何结果不得回滚业务 submit。

#### Scenario: Submit 成功且 Present OutOfDate

- **WHEN** graphics submit 已成功而 `vkQueuePresentKHR` 返回 `VK_ERROR_OUT_OF_DATE_KHR`
- **THEN** `end_frame()` 外层 MUST 成功并返回有效 completion，presentation status MUST 为 `OutOfDate`，业务状态 MUST commit，且下一次有效 frame 前 MUST 执行同步 recreate

#### Scenario: Submit 成功且 Present Suboptimal

- **WHEN** graphics submit 已成功而 `vkQueuePresentKHR` 返回 `VK_SUBOPTIMAL_KHR`
- **THEN** 当前 frame MUST 保持已提交事实，presentation status MUST 为 `Suboptimal`，并安排后续同步 recreate

#### Scenario: Submit 失败

- **WHEN** acquire 成功后 graphics submit 失败
- **THEN** `end_frame()` 外层 MUST 失败，不得返回业务 completion；viewport MUST 锁存 terminal failure，禁止重试使用 acquired image、`image_acquired` 或状态未知的 fence

### Requirement: Abort 闭合 Acquire

acquire 成功后的业务录制失败 MUST 通过 `abort_frame()` 闭合。abort SHALL 丢弃业务 command list/local final state，并以最小 present-layout command buffer submit/present 消费 `image_acquired`；该最小 submit MUST NOT 发布业务资源状态或业务 completion。任一闭合步骤失败 MUST 锁存 terminal。

#### Scenario: Recording 失败后 Abort 成功

- **WHEN** frame-local recording 失败但最小 submit/present 闭环成功
- **THEN** frame slot MAY 按 submission fence 继续周转，pending business resources MUST 保持可重录且本次不得 commit

### Requirement: 不引入部分 Native API Wrapper

`VulkanQueue`、`VulkanSwapchain` 与 `VulkanViewportContext` SHALL 直接调用各自职责内的 Vulkan API。backend MUST NOT 保留只包装部分 acquire/submit/present/create 调用的 `VulkanPresentationNativeApi` 或同义 virtual wrapper，测试 MUST NOT 通过仅调用 mock 自身来声称覆盖真实 viewport orchestration。

#### Scenario: 需要故障注入

- **WHEN** 未来测试需要纯结果映射和真实 validation smoke 无法覆盖的 native 故障注入
- **THEN** 项目 MUST 先形成独立设计，建立能实际驱动 `VulkanSwapchain`/`VulkanViewportContext` 的完整 backend-wide dispatch，而不得在本 capability 中恢复部分 presentation wrapper

### Requirement: 公共 RHI 与跨后端边界不变

方案 A MUST 不改变 `RHIViewportContext`、`RHIFrameContext`、`RHIFrameEndResult` 或普通 `RHIQueue::submit()` contract。`VkSwapchainKHR`、image index、`VulkanSwapchain`、`VulkanSwapchainImage`、`VulkanFrameSlot`、semaphore 和 native fence MUST 全部留在 Vulkan backend，D3D11/D3D12 不得实现或依赖这些 Vulkan 专用类型。

#### Scenario: RenderScene 提交 frame

- **WHEN** RenderScene 使用公共 frame API 完成 draw
- **THEN** 调用链和 submit/presentation status 语义 MUST 与重构前一致，且上层不得按 Vulkan backend 名称分支

### Requirement: 可恢复 Presentation Status 不记录为 Error

`NotReady`、`OutOfDate` 与 `Suboptimal` SHALL 保持可诊断，但 MUST NOT 由
`RHIStatus::failure()` 记录为 error。`NotReady` SHALL 使用 debug，
`OutOfDate`/`Suboptimal` SHALL 使用 info；terminal failure 与调用 contract 错误仍 SHALL 使用 error。
日志分级 MUST NOT 改变公共 status code 或 viewport 恢复控制流。

#### Scenario: 调整窗口导致 Present OutOfDate

- **WHEN** `vkQueuePresentKHR` 因窗口尺寸变化返回 `VK_ERROR_OUT_OF_DATE_KHR`
- **THEN** backend MUST 返回 `OutOfDate` 并安排 recreate，日志 MUST 将其记录为可恢复 info 而不是 RHI error
