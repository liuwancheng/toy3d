## Why

`VulkanDevice` 已经完成公共 `RHIDevice` NVI 前端收敛，但后端实现仍把 native device 生命周期、类型映射、资源与 binding/pipeline 创建，以及 command、viewport、swapchain 对 device 服务的访问集中在一个类和一个大型翻译单元中。现在拆清这些内部职责，可以让 `RHIDevice` 继续作为上层唯一 God facade，同时避免 Vulkan backend 演变为 God implementation，并为 D3D11/D3D12 后端提供可复制的职责形状。

## What Changes

- 保留 `VulkanDevice` 作为 `RHIDevice` 的唯一 Vulkan backend facade、native device 生命周期 owner 和 backend composition root，不新增平行 device、manager 或全局入口。
- 将 Vulkan enum/type mapping、resource/view/shader/sampler native 创建、binding layout/set/packet materialization 和 graphics pipeline 创建拆到 backend-private 的窄模块；`VulkanDevice::create_*_impl()` 只做依赖路由和结果返回。
- 让 `VulkanCommandContext`、`VulkanViewportContext` 与 `VulkanSwapchain` 通过构造参数获得实际所需的 native handles 和 service references，不再把 `VulkanDevice&` 当作 backend service locator。
- 保持公共 RHI API、owner identity、frontend validation、queue completion、presentation、device-lost 与 shutdown 行为不变；不把 Vulkan 类型或新 backend helper 暴露到 renderscene。
- 增加结构边界与回归测试，阻止重新引入对完整 `VulkanDevice` 的隐式依赖或第二套 frontend policy。

## Capabilities

### New Capabilities

无。本 change 是 Vulkan backend 内部职责重构，不新增对上层可观察的能力。

### Modified Capabilities

无。现有公共 RHI、frame submission 与 presentation contract 保持不变；`.openspec.yaml` 使用 `skip_specs: true`。

## Impact

- 主要影响 `engine/runtime/drivers/vulkan/` 的 `vulkan_device.*`、command context、viewport、swapchain、resource/binding/pipeline 创建代码和 CMake source registration。
- 公共 `drivers/rhi/` 接口与 renderscene 调用方式不变。
- 新增或调整 Vulkan backend 单元测试和结构检查；继续使用现有 RHI frontend、resource state、binding、viewport/swapchain 与 Editor Vulkan smoke 作为回归验证。
- 不增加第三方依赖，不修改 VMA、VulkanPortable v1、D3D11 FL11_0 或 D3D12 的公共语义。
