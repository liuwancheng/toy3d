# Toy3d RHI 需求与设计边界

## 文档职责

本文记录已确认的产品需求、架构边界和验收条件，回答“Toy3d RHI 必须是什么”。UE 事实和当前代码缺陷不写入本文。

## 范围

- 支持 Vulkan、D3D10、D3D12；公共接口禁止原生类型和后端名称分支。
- 第一阶段只实现 graphics、单线程录制、单 graphics queue。
- 接口必须允许后续无破坏性接入 compute 和 pass 级多线程录制。
- 单个 pass 内不做并行；多 GPU、ray tracing、VRS、bindless、RDG、RHI thread、async compute 均非第一阶段目标。
- RHI 图形闭环稳定后直接建设完整 RDG，不实现独立的临时 Pass Scheduler。RDG 位于 renderscene，不进入公共 RHI；第一版 RDG 可以分阶段交付，但其资源、pass 和编译模型必须属于同一长期架构。
- API 特有能力通过 capability/扩展表达；不支持返回 `Unsupported`，禁止空操作成功。

## 命名原则

- RHI 的职责划分、行为和通用渲染术语参考 UE4.27，例如 device、command context、command list、graphics pipeline state、render pass、transition、resource lock 和 `ERHIAccess`；不复制 UE 的宏系统、对象系统、`F`/`E` 前缀规则及历史重复接口。
- 保留 Toy3d 现有 `RHI` 类型前缀和 snake_case 函数风格。公共命名表达跨后端语义，不使用 `Vk*`、D3D resource state、descriptor set、root signature、command encoder 等单一 API 或其他框架术语塑造核心接口。
- 创建描述继续使用 `*Desc`；表达一次执行行为或包含资源强引用的临时信息可使用 `*Info`。重命名必须按调用链分阶段迁移，禁止只为表面贴近 UE 产生大范围无关改动。
- 推荐的核心行为命名包括 `set_graphics_pipeline_state()`、`begin_render_pass()`、`end_render_pass()` 和 `transition()`；shader binding 的最终命名在 reflection 与参数模型定型时统一决定，不能直接把 Vulkan descriptor set 命名引入公共层。

## 分层

- 公共 RHI 行为入口使用 non-virtual interface：公共方法统一执行 descriptor validation、capability/limits 检查、规范化、cache、状态机和错误语义，只把不可共享的原生创建或命令翻译路由到受保护的 backend `*_impl()`。禁止 renderscene 绕过公共入口直接调用 backend hook。
- `RHIDevice`：初始化、capability/limits、资源/view/shader/binding layout/pipeline 创建。第一阶段初始化接收含 `primary_surface` 的 device descriptor，Vulkan 必须据此选择同时支持 graphics 和 present 的 queue family；无法满足时返回 `Unsupported`。
- `RHICommandContext`：copy、transition、通用绑定；`RHIGraphicsCommandContext`：render pass、graphics pipeline、draw；预留 `RHIComputeCommandContext`：compute pipeline、dispatch。
- queue：submit 和完成序号。第一阶段只有单 graphics queue；swapchain acquire/present 的 GPU-GPU 同步仅由后端处理，不进入公共资源或普通 submit 描述符。
- `RHIViewportContext`：一个 native surface 的完整 presentation 与帧边界。swapchain 作为它的后端内部组成，不建立与 viewport 平行的公共创建和使用路径。`RHIDevice` 不向 renderscene 暴露 `create_swapchain()`；viewport 在 Render Thread 提供 `begin_frame`、`end_frame`、明确的失败帧收尾语义和延迟 resize，内部处理 acquire、queue submit、present、frame slot、swapchain image 与帧内回收。
- `RHIFrameContext` 只在 begin/end 之间有效，向 renderscene 暴露当前帧的 present texture/view 与帧内 command context 创建；image index、frame slot index、同步 token、queue 和 swapchain 均不得泄漏到 renderscene。
- frame slot 是 CPU/GPU 周转及 command/descriptor/upload 回收域；swapchain image 是 acquire 得到的 presentation 资源；逻辑 frame id 与 queue completion value 分别表示 CPU 帧序号和 GPU 提交完成序号，四者禁止混用。
- pass 依赖、资源声明、调度和后续 RDG 编译属于 renderscene。RHI render pass 只表达 attachment scope，不承担图调度职责。
- 公共头文件不得定义可变全局 RHI 指针，资源不得反向依赖全局 device。
- `RHISurface` 只保存 platform kind 与不透明 native handles，由 platform 层从 `IWindow` 创建；公共 RHI 不得包含 Win32、GLFW、Cocoa、Android 或图形 API 类型。

## Compute 与 Pass 级并行

- 第一阶段可串行复用一个 immediate graphics context，但接口按 acquire/create recording context 设计。
- `ShaderStage`、binding layout、usage/access 从第一版容纳 Compute 和 storage/UAV。
- `compute_dispatch`、`storage_resource/UAV`、`async_compute_queue` 是独立 capability。
- 后续以完整 pass 为并行任务，例如 `ShadowPass` 与 `BasePass`；每个 pass 独占 context/list，结束后不可修改。
- 后续由 RDG 按依赖拓扑规划 pass、跨 pass transition/hazard 和提交顺序。后端不能可靠并行时退化为串行且结果不变。

## Command List 与资源状态权威

- command context 是录制入口；`finish_recording()` 产出结束后不可修改的 command list。command list 必须保留所录制 GPU 工作引用的 resource、view、sampler、shader、binding、pipeline、render-pass backend object 和 upload allocation，直到对应 queue completion value 完成。
- 录制 transition 时不得直接修改资源对象或 device 全局表中的 committed access/layout。每个 command list 使用 local state tracker，至少记录资源第一次使用要求的 initial access、录制过程中的 local access 和结束时的 final access；texture tracker 必须允许演进到 subresource range。
- 同一 graphics queue 的 committed state 按实际提交顺序推进，不需要等待 GPU 完成；queue completion value 只负责 allocator、上传内存、descriptor 和资源销毁等生命周期回收，不能代替资源状态排序。
- command list 丢弃、录制失败或提交失败时不得提交其 final state。第一阶段可由调用方在录制前确定 transition 计划，local tracker 负责验证并输出状态摘要；后续由 RDG 统一规划跨 pass transition，并可在 command-list 边界增加接缝 barrier 编译，但不引入 UE 的完整 RHI thread/command replay 系统。
- 每个 context/list 单线程录制；queue submit 串行化。并行 pass 只能共享不可变对象或使用明确同步的 device cache、thread-local/frame-local pool。

## 资源与生命周期

- buffer/texture descriptor 表达 dimension、extent、format、mip、layer、sample count、usage、CPU access、initial access、debug name，并验证非法组合。
- buffer 保持统一的 `RHIBuffer` 资源类型，以 usage flags 表达允许用途；只有 structured buffer 的 `structure_stride` 属于资源创建描述符，vertex stride 和 index format 属于 binding/view 语义。
- SRV/UAV/RTV/DSV 是独立 view，描述 format、subresource range、depth/stencil 只读属性；render pass 引用 view。
- 公共强引用表达 CPU 所有权；录制单元保留 GPU 工作所需资源。最终释放进入按 queue completion value 管理的 deferred-deletion queue。
- command/descriptor pool、upload ring、临时 framebuffer 按 frame-in-flight/completion value 分代，GPU 完成后才能复用。
- initial data 包含 size、row/slice pitch、所有权和消费时机。
- 公共资源描述使用 `RHICPUAccess` 表达 CPU 是否需要直接访问，不引入 `MemoryDomain` 或公开 Vulkan memory type、D3D heap type。`None` 表示通常由后端选择 GPU-local/default 资源，`Write` 表示 CPU 写入路径，`Read` 表示 readback 路径；不把低效且难以统一的 `ReadWrite` 作为长期核心能力。
- `RHICPUAccess` 只表达访问需求，不承诺具体 heap、persistent mapping 或 coherent memory。后端依据 usage、CPU access 和 capability 选择原生内存；GPU-only 资源更新统一通过 upload/copy，readback 通过 readback resource/ticket 与 completion 查询。

## 同步、上传与映射

- `ERHIAccess` 表达通用用途，不暴露 Vulkan layout/mask 或 D3D12 state；transition 由 command context 录制并预留 subresource range。
- Vulkan/D3D12 生成 barrier；D3D10 跟踪逻辑状态、验证 hazard 并解除冲突绑定。
- map/update 明确 mode、range、alignment、flush/invalidate 和 in-flight 冲突；GPU-only 更新通过 upload/copy，资源对象不得私自 submit 或 wait idle。
- 第一阶段上传通过 frame-local command context 的 `upload_buffer` / `upload_texture` 录制；后端必须在调用返回前将源数据复制到自有 staging storage，并在该帧 fence 完成前保活。`RHIDevice::create_*` 不得为 `initial_data` 隐式 submit 或 wait；未引入初始化批次前必须明确返回 `Unsupported`。
- `RHIGPUFence` 只表示命令流中的 GPU 完成点，用于 CPU 轮询 readback 等需求；创建和写入必须显式，不能作为 swapchain acquire/present semaphore 的公共替代。帧回收统一依赖 `RHIQueueCompletionValue`。

## RHI Render Pass 与 RDG 边界

- `RHIRenderPassInfo` 只描述 color/depth/stencil attachment view、load/store、clear、resolve、render area 和必要的只读属性；Vulkan 可映射 render pass/dynamic rendering，D3D12/D3D10 可用目标绑定、clear、discard 和 resolve 组合实现。
- 公共 RHI 不表达 RDG pass、依赖边、资源 culling、transient aliasing、queue 调度、Vulkan subpass/input attachment 或 tile-local dependency。
- 后续 RDG 必须是 renderscene 的长期设施，统一承担 logical/transient resource、pass read/write 声明、依赖图编译、生命周期、barrier 规划、pass culling、并行录制计划与未来多 queue 调度。可以先实现其中的顺序 graphics 子集，但不得建立一套需要被 RDG 再次替换的临时 Pass Scheduler API。
- 每个 RDG pass 是未来并行录制的最小任务边界；每个 pass 独占 context/list，单个 pass 内保持串行。RDG 生成 RHI transition 与 render-pass scope，RHI 不反向理解 `ShadowPass`、`BasePass` 等上层业务语义。

## Binding、Shader 与 Pipeline

- `RHIBindingLayout` 使用 resource type、shader stage、slot、array count，不暴露 descriptor set/heap/root parameter。
- `RHIBindingGroup` 只表达资源更新频率和所有权分组，不创建独立的 shader 寄存器命名空间；同一 stage 内重叠的 b/t/s/u 同类 slot 即使属于不同 group 也必须拒绝，以保证 D3D10 可直接映射。
- sampler descriptor 只包含三后端共有的 filter、address mode、LOD、anisotropy、comparison 和固定 border color 语义；后端在创建前检查 capability 与 limits。
- shader 输入包含 stage、目标字节码、entry point、reflection 和稳定 content hash。
- pipeline descriptor 是完整不可变值；cache key 覆盖全部兼容状态，hash 命中后做 equality 校验。
- graphics pipeline cache 由 device 拥有并在公共 RHI frontend 实现。它使用不含对象地址和 debug name 的规范化值键，对并发 miss 做 single-flight 去重；确定性 validation 在进入 cache 前完成，backend 创建失败不永久缓存。shutdown 必须拒绝新的 pipeline 创建并等待已进入的创建结束，再等待 GPU idle、释放 cache 强引用并销毁 native device。
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
- command list 被丢弃、录制失败或提交失败不会污染 committed resource state；按不同录制顺序生成 command list 后，实际提交顺序仍能得到确定且可验证的 transition 结果。
- shader、binding layout、pipeline 使用稳定完整键，hash collision 不返回错误对象。

## 演进顺序

1. definitions、descriptor、capability/limits、错误模型。
2. device、graphics context、queue、viewport/presentation 分层；swapchain 收入 viewport 内部。
3. 资源/view、上传、command-list-local transition、提交状态推进、延迟销毁。
4. graphics render pass、pipeline、binding、draw/copy 闭环。
5. 在 renderscene 建设正式 RDG，先闭环 graphics pass 声明、依赖编译、barrier、生命周期和串行录制，不建立临时 Pass Scheduler。
6. RDG pass culling、transient pool/aliasing及每线程 context/pool与pass级并行录制按依赖继续演进。
7. compute pipeline/context 和 RDG compute pass；async compute/multi-queue及其他高级能力独立后置。

## Vulkan 启动边界

- `VulkanDevice` 从 `RHIDeviceDesc::primary_surface` 创建 Vulkan instance 和 primary `VkSurfaceKHR`，只选择同时支持 graphics 与 present 的单一 queue family；不满足时返回 `Unsupported`。
- 设备销毁顺序固定为 queue/device idle、queue 和其他 device 子对象、logical device、primary surface、instance。未实现的 Vulkan API 必须返回 `Unsupported`，不得伪装为成功。
- Vulkan backend 使用独立构建选项，关闭该选项时不得把任何 Vulkan 源文件加入运行时 target。
