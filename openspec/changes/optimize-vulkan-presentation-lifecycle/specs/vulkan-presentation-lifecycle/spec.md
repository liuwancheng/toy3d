## Purpose

定义 Vulkan viewport 在多帧并行、swapchain 重建及不同 WSI 能力下安全管理 presentation 同步对象和 generation retirement 的行为，并保持公共 RHI presentation 边界不泄漏原生对象。

## ADDED Requirements

### Requirement: Per-image presentation semaphore ownership

Vulkan viewport SHALL 将 `render_finished` semaphore 归属到 swapchain image 的 backend-private presentation state，而非 frame slot。graphics submit completion SHALL NOT 单独证明该 semaphore 可复用或可销毁。

#### Scenario: 成功 present 后再次 acquire 同一 image

- **WHEN** image `i` 的 present 成功进入 WSI，且后续成功 acquire 同一 generation 的 image `i`
- **THEN** viewport MAY 复用 image `i` 的 `render_finished` semaphore，并保持其他 image 的 in-flight presentation 不受影响

#### Scenario: submit fence 先完成

- **WHEN** image `i` 的 graphics submit completion fence 已 signal，但尚未取得同 image reacquire 或 present fence proof
- **THEN** viewport MUST NOT 复用或销毁 image `i` 的 `render_finished` semaphore

### Requirement: Present-result failure handling

Vulkan viewport SHALL 根据 submit 与 `vkQueuePresentKHR` 的结果维护 acquired image、semaphore 和 terminal failure 状态；不得假定失败的 present 已由 WSI 消费 wait semaphore。

#### Scenario: 成功 submit 后 present 返回 OutOfDate

- **WHEN** graphics submit 已成功且 `vkQueuePresentKHR` 返回 `VK_ERROR_OUT_OF_DATE_KHR`
- **THEN** viewport MUST 在对应 submit fence 完成后只销毁该 image 的不确定 `render_finished` semaphore，MUST NOT 复用它，并安排 swapchain recreate

#### Scenario: submit 失败

- **WHEN** acquire 成功后 graphics submit 失败
- **THEN** viewport MUST 锁存 terminal failure，且不得重试使用该 acquired image、acquire semaphore 或状态不确定的同步对象

#### Scenario: abort frame

- **WHEN** acquire 成功后的业务录制被 abort
- **THEN** viewport MUST 以最小 submit/present 闭环消费 acquire synchronization；若不能完成，MUST 锁存 terminal failure

### Requirement: Optional swapchain maintenance capability gate

Vulkan viewport SHALL 在 VulkanPortable v1 基线无扩展时保持正确可用。`VK_EXT_swapchain_maintenance1` 仅在其 instance/device dependency 与 `swapchainMaintenance1` feature 均已启用时 MAY 用于 present-fence retirement。

#### Scenario: Extension 可用且 feature 启用

- **WHEN** `VK_KHR_get_surface_capabilities2`、`VK_EXT_surface_maintenance1`、`VK_KHR_swapchain` 和 `VK_EXT_swapchain_maintenance1` 已按其层级启用，且 `swapchainMaintenance1` feature 为真
- **THEN** viewport MAY 为成功提交给 WSI 的 present 关联 present fence，并在该 fence signal 后退休对应 presentation objects

#### Scenario: 任一依赖或 feature 不可用

- **WHEN** 任一所需 extension 或 `swapchainMaintenance1` feature 不可用或启用失败
- **THEN** viewport MUST 回退至 Vulkan 1.1 per-image reacquire 路径，且公共 RHI 结果和错误语义保持可用

### Requirement: Generation-based swapchain recreation

Vulkan viewport SHALL 以 backend-private generation 管理 swapchain objects。新 generation MUST 在完整构造成功后才替换 active generation；常规 resize 或 `OutOfDate` 路径 MUST NOT 使用 `vkDeviceWaitIdle()` 作为退休条件。

#### Scenario: 无扩展的旧 generation 退休

- **WHEN** 旧 generation 仍有无法通过后续 acquire 证明完成的 WSI presentation，且当前后端使用单一 shared graphics/present queue
- **THEN** viewport MUST 在销毁该 generation 前等待该 queue idle，并在 queue wait 成功后退休其 presentation objects；MUST NOT 调用 `vkDeviceWaitIdle()`

#### Scenario: 新 generation 构造失败

- **WHEN** 新 swapchain 或其任一 image view、synchronization object 或 RHI wrapper 创建失败
- **THEN** viewport MUST NOT 发布半初始化 generation，且不得提前销毁仍可能被 GPU 或 WSI 使用的旧 generation 对象

### Requirement: Frame slot 与 image identity 独立管理

Vulkan viewport SHALL 将 CPU/GPU frame slot 数量与 swapchain image identity 分离。slot 数 MUST 为 `min(max_frames_in_flight, actual_image_count)`，其中默认 `max_frames_in_flight` 为 backend-private 值 `2`；image 的上次 graphics use 与 presentation retirement MUST 按 acquired image identity 跟踪。

#### Scenario: 实际 image 数多于 slot 上限

- **WHEN** surface 返回的 actual image count 大于 `max_frames_in_flight`
- **THEN** viewport MUST 限制 CPU frame ahead 深度，同时仍可正确 acquire、render 和 present 任一可用 image
