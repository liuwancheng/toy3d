# Toy3d RHI 需求与设计边界

## 文档职责

本文记录已确认的产品需求、架构边界和验收条件，回答“Toy3d RHI 必须是什么”。UE 事实和当前代码缺陷不写入本文。

## 范围

- 支持 Vulkan、D3D10、D3D12；公共接口禁止原生类型和后端名称分支。
- 第一阶段只实现 graphics、单线程录制、单 graphics queue。
- 接口必须允许后续无破坏性接入 compute 和 pass 级多线程录制。
- 单个 pass 内不做并行；多 GPU、ray tracing、VRS、bindless、完整 render graph、RHI thread、async compute 均非第一阶段目标。
- API 特有能力通过 capability/扩展表达；不支持返回 `Unsupported`，禁止空操作成功。

## 分层

- `RHIDevice`：初始化、capability/limits、资源/view/shader/binding layout/pipeline 创建。第一阶段初始化接收含 `primary_surface` 的 device descriptor，Vulkan 必须据此选择同时支持 graphics 和 present 的 queue family；无法满足时返回 `Unsupported`。
- `RHICommandContext`：copy、transition、通用绑定；`RHIGraphicsCommandContext`：render pass、graphics pipeline、draw；预留 `RHIComputeCommandContext`：compute pipeline、dispatch。
- queue：submit 和完成序号；swapchain：acquire、present、resize、back buffer。swapchain acquire/present 的 GPU-GPU 同步仅由后端处理，不进入公共资源或 submit 描述符。
- `RHIViewportContext`：一个 native surface 的帧边界；在 Render Thread 提供 `begin_frame`、`end_frame` 和延迟 resize。它内部处理 acquire、queue submit、present、frame slot 与帧内回收。`RHIFrameContext` 只在 begin/end 之间有效，向 renderscene 暴露当前帧的 present texture/view 与帧内 command context 创建；image index、同步 token、queue 和 swapchain 不得泄漏到 renderscene。
- frame：CPU/GPU 周转与帧内资源回收域。pass 依赖、资源声明和调度属于 renderscene。
- 公共头文件不得定义可变全局 RHI 指针，资源不得反向依赖全局 device。
- `RHISurface` 只保存 platform kind 与不透明 native handles，由 platform 层从 `IWindow` 创建；公共 RHI 不得包含 Win32、GLFW、Cocoa、Android 或图形 API 类型。

## Compute 与 Pass 级并行

- 第一阶段可串行复用一个 immediate graphics context，但接口按 acquire/create recording context 设计。
- `ShaderStage`、binding layout、usage/access 从第一版容纳 Compute 和 storage/UAV。
- `compute_dispatch`、`storage_resource/UAV`、`async_compute_queue` 是独立 capability。
- 后续以完整 pass 为并行任务，例如 `ShadowPass` 与 `BasePass`；每个 pass 独占 context/list，结束后不可修改。
- queue 按依赖拓扑提交；跨 pass transition/hazard 由统一调度层规划。后端不能可靠并行时退化为串行且结果不变。

## 资源与生命周期

- buffer/texture descriptor 表达 dimension、extent、format、mip、layer、sample count、usage、CPU access、initial access、debug name，并验证非法组合。
- SRV/UAV/RTV/DSV 是独立 view，描述 format、subresource range、depth/stencil 只读属性；render pass 引用 view。
- 公共强引用表达 CPU 所有权；录制单元保留 GPU 工作所需资源。最终释放进入按 submit serial/fence 管理的 deferred-deletion queue。
- command/descriptor pool、upload ring、临时 framebuffer 按 frame-in-flight/serial 分代，fence 完成后才能复用。
- initial data 包含 size、row/slice pitch、所有权和消费时机。

## 同步、上传与映射

- `ERHIAccess` 表达通用用途，不暴露 Vulkan layout/mask 或 D3D12 state；transition 由 command context 录制并预留 subresource range。
- Vulkan/D3D12 生成 barrier；D3D10 跟踪逻辑状态、验证 hazard 并解除冲突绑定。
- map/update 明确 mode、range、alignment、flush/invalidate 和 in-flight 冲突；GPU-only 更新通过 upload/copy，资源对象不得私自 submit 或 wait idle。
- 第一阶段上传通过 frame-local command context 的 `upload_buffer` / `upload_texture` 录制；后端必须在调用返回前将源数据复制到自有 staging storage，并在该帧 fence 完成前保活。`RHIDevice::create_*` 不得为 `initial_data` 隐式 submit 或 wait；未引入初始化批次前必须明确返回 `Unsupported`。
- `RHIGPUFence` 只表示命令流中的 GPU 完成点，用于 CPU 轮询 readback 等需求；创建和写入必须显式，不能作为 swapchain acquire/present semaphore 的公共替代。帧回收统一依赖 queue submit serial。

## Binding、Shader 与 Pipeline

- `RHIBindingLayout` 使用 resource type、shader stage、slot、array count，不暴露 descriptor set/heap/root parameter。
- `RHIBindingGroup` 只表达资源更新频率和所有权分组，不创建独立的 shader 寄存器命名空间；同一 stage 内重叠的 b/t/s/u 同类 slot 即使属于不同 group 也必须拒绝，以保证 D3D10 可直接映射。
- sampler descriptor 只包含三后端共有的 filter、address mode、LOD、anisotropy、comparison 和固定 border color 语义；后端在创建前检查 capability 与 limits。
- shader 输入包含 stage、目标字节码、entry point、reflection 和稳定 content hash。
- pipeline descriptor 是完整不可变值；cache key 覆盖全部兼容状态，hash 命中后做 equality 校验。
- graphics pipeline descriptor 必须显式包含 vertex buffer/attribute、primitive topology、rasterization、depth/stencil、每 color attachment 的 blend/write mask，以及 attachment format/sample count；viewport 和 scissor 属于 command context 的动态状态。
- graphics pipeline 必须兼容实际 attachment 的 format、sample count、load/store/resolve 与 depth/stencil 用法。

## Capability、错误与线程

- capability 同时包含功能 bool 与 format usage、MSAA、alignment、attachment/slot/dimension 等 limits。
- 初始化、创建、map/update、acquire、submit、present、resize 返回可检查结果；assert 不能代替 Release 错误路径。
- device lost 后停止原生调用并进入统一恢复/终止路径；device 晚于子资源、swapchain、pool、cache 销毁。
- 明确 device、queue、context、资源、cache 的线程归属；mutable state 必须 context-local 或内部同步。

## 第一阶段验收

- 无 validation error 完成 acquire、render pass、draw、submit、present；resize/out-of-date 可恢复。
- 创建、上传、transition、view、延迟销毁闭环；多 frame-in-flight 不提前复用资源。
- 每个 pass 可形成独立、结束后不可修改的录制单元，即使当前串行执行。
- shader、binding layout、pipeline 使用稳定完整键，hash collision 不返回错误对象。

## 演进顺序

1. definitions、descriptor、capability/limits、错误模型。
2. device、graphics context、queue/swapchain 分层。
3. 资源/view、上传、transition、延迟销毁。
4. graphics render pass、pipeline、binding、draw/copy 闭环。
5. pass 资源声明和依赖显式化，仍串行录制。
6. 每线程 context/pool 与 pass 级并行录制。
7. compute pipeline/context；其他高级能力独立后置。

## Canonical Vulkan 启动边界

- `VulkanDevice` 从 `RHIDeviceDesc::primary_surface` 创建 Vulkan instance 和 primary `VkSurfaceKHR`，只选择同时支持 graphics 与 present 的单一 queue family；不满足时返回 `Unsupported`。
- 设备销毁顺序固定为 queue/device idle、queue 和其他 device 子对象、logical device、primary surface、instance。未实现的 canonical Vulkan API 必须返回 `Unsupported`，不得伪装为成功。
- canonical Vulkan backend 使用独立构建选项，旧 `IDynamicRHI` 原型继续排除，二者不得在同一 target 中混编。
