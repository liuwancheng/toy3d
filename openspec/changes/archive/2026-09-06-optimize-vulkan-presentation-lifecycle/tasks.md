## 1. 规范与迁移边界

- [x] 1.1 更新 `document/rhi-design.md` 与 `document/vulkan-memory-management.md`：登记 `VulkanViewportContext + VulkanSwapchain` owner 链、正式类型/成员命名、同步 shared-queue recreate、maintenance1 删除和未来独立 present queue 的重新设计条件。
- [x] 1.2 盘点 `VulkanGenerationLifecycle`、`VulkanGenerationPublicationTracker`、`VulkanPresentationNativeApi`、maintenance1 capability/ABI、retired generations、present fences 及旧测试/CMake 的全部调用点，形成单批次删除清单，禁止保留兼容 alias 或同义 wrapper。

## 2. VulkanSwapchain 所有权

- [x] 2.1 新增 backend-private `VulkanSwapchain` 与 `VulkanSwapchainImage`，迁移 `VkSwapchainKHR`、surface format/extent、images/views、RHI texture/view wrapper 和 per-image `rendering_done` semaphore 的创建、查询与 RAII 销毁。
- [x] 2.2 将 acquire 和 present 收入 `VulkanSwapchain::acquire_image()` / `present()`，显式映射 Success、`Suboptimal`、`OutOfDate`、`DeviceLost` 和其他 terminal error；保持 per-image semaphore 只在同 image 再次成功 acquire 后复用。
- [x] 2.3 删除 `SwapchainGeneration`、双份 per-image state、`VulkanImagePresentationPhase`、`VulkanPresentTransition`、`VulkanGenerationLifecycle` 和 `VulkanGenerationRetirementMode`，确认不存在旧名称或第二套 presentation owner。

## 3. Viewport 与 FrameSlot

- [x] 3.1 将 viewport-private `FrameSlot` 重命名为 `VulkanFrameSlot`，将成员统一迁移为 `image_acquired`、`submission_fence`、command pool、present-transition command buffer、completion value 和提交 payload；保持 slot 数为 `min(2, actual_image_count)`。
- [x] 3.2 让 `VulkanViewportContext` 显式拥有 `std::unique_ptr<VulkanSwapchain>` 与 `VulkanFrameSlot` 集合，迁移 begin/end/abort 的 frame identity、image `last_submission_fence`、payload retain、completion 和 terminal status 逻辑。
- [x] 3.3 保持 `abort_frame()` 最小 transition/submit/present 闭环，验证其不提交业务 command list、不发布业务资源状态，且任何 acquire 后闭合失败都会锁存 terminal。

## 4. 同步 Recreate 与旧机制删除

- [x] 4.1 将 resize、`Suboptimal` 和 `OutOfDate` 收敛到后续 `begin_frame()` 干净边界的同步 recreate：zero extent 返回 `NotReady`，其余路径只等待当前 shared graphics/present queue idle，完整构造 replacement 后再发布。
- [x] 4.2 删除 `retired_generations`、present fences、deferred present fences、maintenance1 extension/feature gate、自定义 maintenance1 ABI structs 与相关 observation；常规 recreate 不得调用 `vkDeviceWaitIdle()`。
- [x] 4.3 删除 `VulkanGenerationPublicationTracker`，让 viewport observation 直接读取实际 swapchain/frame-slot owner，并只保留确有诊断价值的普通历史计数。

## 5. Native 调用边界与测试

- [x] 5.1 删除 `VulkanPresentationNativeApi`、`VulkanPresentationNativeApiDefault`、`VulkanDevice::presentation_native_api()` 和 device/queue 对 wrapper 的 ownership；`VulkanQueue`、`VulkanSwapchain`、`VulkanViewportContext` 分别直接调用职责内 Vulkan API。
- [x] 5.2 将旧 `vulkan_presentation_lifecycle_tests` 重构为 swapchain result、frame-slot/image identity、public submit/present/abort status 测试；删除只验证 mock 计数、publication tracker 和已移除 lifecycle 类型的断言。
- [x] 5.3 更新 runtime CMake 源码和测试登记，确认 `vulkan_presentation_lifecycle.*`、旧测试 target/文件、maintenance1 名称和 compatibility alias 无残留。
- [x] 5.4 将公共 RHI 可恢复 viewport status 日志分级为 `NotReady=debug`、`OutOfDate/Suboptimal=info`，保留 terminal 与调用 contract failure 的 error 诊断，且不改变 status/control-flow contract。

## 6. 验证

- [x] 6.1 重新配置并构建受影响 runtime、Editor 和 Vulkan 定向测试 targets，运行受影响 CTest。
- [x] 6.2 在 validation layer 下执行正常多帧、两个 slots/至少三张 images、resize、minimize/restore、连续 `OutOfDate`、abort、正常 shutdown 和 terminal teardown smoke，确认无 semaphore/fence 复用或 in-flight destruction 错误。
- [x] 6.3 由独立 sub-agent 使用 `verify-toy3d-build` 复核配置、构建、测试、文档/代码命名残留和 Vulkan smoke；修复其发现的问题后再交付。
