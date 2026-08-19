# Toy3d RHI 当前实现审查

## 文档职责

本文记录截至 2026-08-19 的代码现状问题，是可更新的迁移清单，不是长期需求。旧 `IDynamicRHI` 原型与 legacy Vulkan 实现已经删除，当前 Vulkan 后端实现 `RHIDevice` 的 backend hooks。

## P0

当前没有未完成项。

## 已完成

1. 已删除公共 `RHISwapchain`、`RHIDevice::create_swapchain()` 以及未完成的 `VulkanSwapchain` 平行路径。presentation 唯一公共入口为 `RHIViewportContext`，Vulkan 的 swapchain、acquire、submit、present 和 resize 继续由 `VulkanViewportContext` 内部管理。
2. Vulkan command list 已使用 local buffer/texture state tracker；录制 transition、copy、upload、render pass 和 binding 不再提前修改 committed state。queue/viewport 在原生提交前验证 initial state，并仅在 `vkQueueSubmit` 成功后提交 final state；丢弃、录制失败和提交失败不会污染资源状态。viewport 的隐式 present barrier 读取 command list 的最终局部 layout。
3. 公共 `RHIViewportContext::abort_frame()` 已明确消费 acquire 后无法继续录制的失败帧。Vulkan 通过最小提交消费 acquire semaphore、恢复并提交 backbuffer `Present` 状态、尝试 present，并推进 frame slot；renderscene 失败路径不再使用空 command-list 的 `end_frame()` 隐式收尾。
4. Vulkan frame slot 在提交成功后直接强持有完整 command list，直到 completion fence 后才释放，以 command list 作为所有录制期 GPU payload 的统一生命周期根。pipeline descriptor 传递持有 shader 和 binding layout，binding set descriptor 传递持有 layout、buffer、texture view 和 sampler，binding set 自身持有 descriptor pool/set；upload page 另按 completion value 退休。Buffer View 当前明确返回 `Unsupported`，不存在未保活的已录制路径。
5. 公共 `RHICPUAccess` 已收紧为 `None`、`Read`、`Write` 三种访问需求并移除无调用方的 `ReadWrite`。该枚举不选择 native heap/memory type，也不承诺 persistent mapping；Vulkan 在 readback/map 闭环实现前对 CPU-accessible resource 明确返回 `Unsupported`。
6. texture transition 已支持 `RHISubresourceRange` 的 aspect、mip 与 array layer 精确范围。Vulkan texture committed state 按 subresource 保存，command list 以稀疏 delta 记录 initial/final state，barrier 使用精确 `VkImageSubresourceRange`；copy、upload、render-pass attachment 与 sampled binding 均按实际访问范围验证，混合状态范围明确报错，提交失败仍不发布 final state。
7. graphics pipeline 创建已迁移为 `RHIDevice::create_graphics_pipeline()` 公共 NVI 与 backend `create_graphics_pipeline_impl()`。公共 frontend 统一执行 validation、limits/format capability、descriptor canonicalization，并使用 pointer-free 完整键、hash collision equality 和并发 single-flight 的 device-owned cache；lifecycle gate 使 shutdown 拒绝新的 pipeline 创建、等待已进入创建结束，再等待 GPU idle、释放 cache 并销毁 native device。
8. stencil pipeline 语义已收敛为 front/back operation 加共用 8-bit read/write mask，stencil reference 移到 command context 动态状态；constant blend factor 对应的 blend constants 也已补为动态命令，Vulkan pipeline 显式声明并录制这两类 dynamic state。
9. Vulkan graphics pipeline 已映射 `VkPipelineDepthStencilStateCreateInfo`，compatibility render pass 与 command-list-local render pass 支持 color+depth 和 depth-only attachment、depth/stencil load/store/clear、只读/可写 layout 与 access 校验，并将 attachment format、sample count 和只读写入兼容性检查。`VulkanPortable v1` 不要求 separate depth/stencil layouts，当前 backend path 对 packed depth/stencil format 的一个 aspect 只读、另一个可写明确返回 `Unsupported`。renderscene test pass 已接入 `D32Float` depth attachment，实际经过创建、transition、clear 和 depth-enabled draw 路径。
10. 已迁移并删除 `RHIFormat`、`RHIAccess` 的 legacy spelling alias，公共枚举只保留规范名称，避免 cache key、日志和后端转换出现同值异名。
11. graphics binding 已使用 `RHIGraphicsBindings` 原子提交完整 logical 快照；Vulkan logical `RHIBindingSet` 不再等同于 `VkDescriptorSet`，而是在 draw 前按 pipeline layout materialize physical packet。Global+View 原子聚合为 set 0，packet 与 source sets 由 command list 保活到 queue completion；旧 `bind_binding_set()` 和未实现诊断已删除。runtime Vulkan API 基线同步为 1.1，与 ShaderCompiler 的 SPIR-V 1.3 contract 一致。代码、生成映射和自动测试已验证；独立 Editor/Vulkan 冒烟已连续两轮完成 draw/present、正常 `WM_CLOSE`、日志刷新与退出码 0，未产生新的 validation warning/error。该证据不代替截图或像素级视觉验收。
12. Vulkan viewport 的失败帧闭环已补齐：acquire `OutOfDate` 不再重建后复用失效的 frame-slot 引用重试；recording/validation failure 通过 `abort_frame()` 的最小 present transition submit 消费 acquire synchronization。acquire 后在 command-buffer begin/end、fence reset、queue submit 等任一步失败都会锁存 terminal presentation failure，可恢复 code 会提升为 `BackendFailure`；重复 command list 在 native submit 前拒绝，submit 后即使 command-list CPU 状态发布异常仍会尝试 present 消费同步。
13. Vulkan device-level graphics context 已实现：每个 context 创建独立 transient command pool，command list 强持有 pool 到 generic queue completion；queue submit 保活 command list、提交 local final state、标记资源与 upload page 的 completion value，并只接受 device-level list，防止 viewport list 绕过 acquire/present 路径。真实 Win32/Vulkan 集成测试已完成 buffer 创建、upload、transition、submit、wait 与 shutdown，无 validation error。
14. Renderer composition root 已使用 device-level context 显式 bootstrap Error Material 所需 checkerboard、white 与 normal textures：三张资源只在单次 submit completion 成功后共同发布，任一步失败保留原始 `RHIErrorCode` 且不部分发布；普通 frame upload 不等待。真实 Vulkan 集成测试与错误注入测试已覆盖该 contract。

## P1

1. CPU map/unmap 尚未定义统一 lock mode、range、alignment、flush/invalidate及in-flight冲突；应与 GPU fence/readback 能力一并定型，禁止资源对象私自 submit 或 wait idle。
2. Vulkan placeholder texture bootstrap 已接入 Renderer composition root；ImGui font bootstrap 尚未实现。D3D11/D3D12 仍需按既定 device-level context、immutable packet 与真实 GPU completion contract 实现对应 backend，未实现路径必须继续明确返回 `Unsupported`。
3. `create_buffer/create_texture(initial_data)` 在 Vulkan 明确返回 `Unsupported`，该诊断继续保留。普通更新使用 frame-local upload；placeholder/font 改由已确认的 device-level bootstrap context 显式录制、submit 与等待 completion，不改变资源创建接口的无隐式提交 contract。
4. Buffer View、storage binding、resolve attachment和GPU fence/readback尚未闭环；其 capability和错误路径需要与Vulkan、D3D11、D3D12映射一起定型。
5. 正式 Renderer 与 RDG 均尚未实现。下一阶段按 `document/rendering-engine-foundation-design.md` 先建设 World/RenderScene、Game/Render Thread、Material、Forward Renderer、PostProcess 与 ImGui；显式 SceneRenderer/业务 Pass 是长期职责，不新增通用临时 Pass Scheduler。RDG 后置，在真实跨 Pass 依赖形成后再接管资源声明、barrier 与调度。
6. 当前只完成 graphics pipeline 创建链与 queue submit 的 NVI；其他 resource/view/shader/binding 创建入口以及 command context 行为仍由 backend 直接实现完整接口。后续应按调用链迁移公共 validation、状态机和资源保活，不能让新后端复制 Vulkan frontend policy。
7. `RHIObject` 尚无不可变 device ownership identity。backend 的 `dynamic_pointer_cast` 只能拒绝不同类型，不能拒绝来自另一个同类型 device 的对象；在支持多 device 或 D3D backend 前应增加公共 owner token/id，并在 NVI frontend 统一检查。

## P2

1. 当前公共行为命名混用`Desc`、`Info`、`set_graphics_pipeline`、`transition_resources`等风格；按UE语义和Toy3d命名规则分调用链迁移，不做一次性无关重命名。
2. shader reflection、binding参数模型、shader/binding layout cache与persistent backend pipeline cache仍需成熟；最终公共命名不能泄漏descriptor set/root signature概念。
3. binding layout entry 与 vertex input vector 尚未定义确定性排序规则；语义等价但排列不同的 descriptor 可能生成重复 PSO。应在各自 descriptor 创建边界统一顺序，不能只在 pipeline key 中排序而改变当前 vertex binding 位置语义。
