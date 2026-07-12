# Toy3d RHI 当前实现审查

## 文档职责

本文记录 2026-07 的代码现状问题，是可更新的迁移清单，不是长期需求。

## P0

1. `rhi.h` 的 `IDynamicRHI` 混合 device、frame、命令和资源创建，并在头文件定义 `g_rhi`。
2. `set_stencil`、`set_blend_factor`、graphics `set_shader_parameter` 默认空实现。
3. `ERHIAccess` 没有公共 transition 命令；Vulkan texture 自行维护 layout，buffer update 缺 barrier。
4. Vulkan 初始化结果不可检查；`VulkanContext::clear()` 的 instance/device/swapchain 销毁顺序错误。
5. acquire/submit/present 缺少完整 resize、semaphore、fence reset/serial 契约。
6. render pass 未录制；pipeline attachment、descriptor layout、push constant 仍为硬编码示例。

## P1

1. render-pass/shader-state descriptor 使用裸资源指针，缺少 view/subresource 和录制期强引用。
2. `RHIResourceCreateInfo::bulk_data` 缺少 size、pitch、所有权；`debug_name` 是借用指针。
3. map/unmap 未定义统一 mode、range、alignment 和 in-flight 冲突。
4. descriptor pool reset 无 fence 约束；binder 保存裸指针；32-bit layout hash 命中后不比较完整键。
5. pipeline cache key 使用对象地址和不完整状态。
6. 缺少统一 `RHICapabilities/RHILimits`，固定最大值和 assert 代替创建前验证。

## P2

1. 公共枚举包含 ray tracing、VRS、patch topology、subpass hint 等当前非目标语义。
2. texture 的虚拟 `cast_texture*()` 存在可疑 override 返回 `nullptr`。
3. 公共头依赖大型 `pch.h`，部分 const/override 和头文件静态定义需要整理。
4. shader reflection 未落地，Vulkan descriptor layout 仍硬编码。
