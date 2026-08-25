## 1. 设计与测试边界

- [x] 1.1 更新 `document/rhi-design.md` 与 `document/vulkan-memory-management.md` 的 viewport 生命周期摘要：明确 frame slot、per-image presentation state、submit completion 与 WSI completion 的边界，以及 VulkanPortable v1 fallback。
- [x] 1.2 为 acquire、submit、present、queue wait 与 native object 创建建立 backend-private 定向测试 seam；定义成功、`Suboptimal`、`OutOfDate`、submit failure 与 abort 的状态表测试。

## 2. Vulkan 1.1 基线路径

- [x] 2.1 重构 `VulkanViewportContext`：FrameSlot 不再拥有 `render_finished`；每个 swapchain image 拥有 presentation state、`render_finished` 与 image fence，slot 数限制为 `min(2, actual_image_count)`。
- [x] 2.2 接入同 image 成功 reacquire 后的 semaphore 复用；submit 成功但 present 未获 WSI completion proof 时禁止复用，并在规定条件下仅销毁。
- [x] 2.3 覆盖 abort、submit failure、present `Suboptimal`、`OutOfDate` 和不可恢复失败，验证 acquired image 与同步对象不会进入未知状态后重试。

## 3. Generation recreation

- [x] 3.1 将 active swapchain resources 封装为 viewport-private generation；新 generation 在临时对象中完整构造并成功后发布，失败不发布半初始化状态。
- [x] 3.2 删除常规 recreate 的 `vkDeviceWaitIdle()`；在当前 shared graphics/present queue 基线中用 `VulkanQueue::wait_idle()` 退休无扩展旧 generation，并保留 device idle 仅用于 shutdown 或 terminal cleanup。
- [x] 3.3 添加 resize、minimize/restore、连续 `OutOfDate`、generation 构造失败和 shutdown 的定向测试与 observation 断言。

## 4. Optional maintenance1 acceleration

- [x] 4.1 在 `VulkanDevice` 实现 instance/device extension 枚举与启用、`VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT` feature query，以及 `VkDeviceCreateInfo::pNext` capability gate；缺少任一依赖时回退基线。
- [x] 4.2 在 capability 启用时为成功 present 接入 `VkSwapchainPresentFenceInfoEXT`，以 fence signal 异步退休旧 generation；验证 enabled/disabled 两条路径的公共 `RHIFrameEndResult` 与错误语义一致。

## 5. 验证

- [x] 5.1 运行 Vulkan viewport 生命周期定向测试，覆盖交错 image、两 slot 限制、全部 present 结果、extension enabled/disabled、resize/minimize/restore 与 shutdown。
- [x] 5.2 重新配置并构建受影响 targets，运行受影响 CTest；在 validation layer 下执行 Vulkan runtime smoke，确认无同步对象复用或 in-flight destruction 错误。
- [x] 5.3 将最终构建、测试、文档 contract 与 runtime smoke 交给独立 sub-agent 使用 `verify-toy3d-build` 复核；修复其发现的问题后再交付。
