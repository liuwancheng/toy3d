# Toy3d RHI 设计

> viewport frame-end、业务 submit、presentation status、abort 和资源提交后发布语义以
> `openspec/specs/game-render-framework/rhi-frame-submission/spec.md` 为当前规范；本文继续定义公共 RHI、资源、binding、pipeline 和跨后端 contract。

## 1. 文档目的

本文定义 Toy3d 的 Render Hardware Interface（RHI）公共架构、Render 上层调用方式、资源与同步模型，以及 Shader、Binding、Pass 和 Material 系统之间的边界。后续 RHI 与各图形 API 后端按本文分阶段实现。

本文的目标后端为 Vulkan、Direct3D 11 和 Direct3D 12。若历史文档或代码仍使用 Direct3D 10，应在实现前统一修正为 Direct3D 11；公共接口不得依赖某个后端的原生类型或行为。

UE4.27 用于参考职责分层、GlobalShader、MeshPassProcessor、MeshDrawCommand 和 RHI command list 的组织方式，但 Toy3d 不复制 UE 的宏系统、对象系统、RHI thread、完整 Render Dependency Graph（RDG）或历史兼容接口。上层正式采用 `Material → MaterialInstance → MaterialRenderProxy`；其后端绑定必须经过公共 RHI。

## 2. 第一阶段范围

第一阶段实现以下闭环：

- 单 graphics queue；
- 单线程录制；
- pass 级独立 recording unit；
- swapchain acquire、submit、present 和 resize；
- buffer、texture、view、sampler 和 shader；
- upload、copy、resource transition 和 deferred deletion；
- graphics pipeline、binding、render pass、draw 和 draw indexed；
- capability、limits、可检查错误和 device lost 路径；
- GlobalShader 驱动的 test/fullscreen pass；
- 独立 BasePass 模块、`MeshPassDrawList` 和 `MeshDrawCommand`。

第一阶段不实现 async compute、bindless、ray tracing、VRS、多 GPU、完整 Render Graph、RHI thread 和 draw batch 内并行。公共描述符和 binding layout 需要预留 compute 与 storage resource，但未实现功能必须返回 `Unsupported`，不得空操作成功。

RHI 图形闭环后的当前上层路线以 OpenSpec change `establish-game-render-framework` 为准：先完成 World/RenderScene、Game/Render Thread、资源生命周期与显式 SceneRenderer 骨架。Forward Renderer、PostProcess、ImGui 与后续 RDG 的具体能力分别进入后续 Spec；期间不建立通用临时 Pass Scheduler。

## 3. 总体分层

```text
GameScene / Editor
        |
        v
RenderScene
    - scene visibility
    - render pass scheduling
    - BasePass / ShadowPass / post process
        |
        v
RenderCore
    - GlobalShaderMap / MaterialShaderMap
    - Material / MaterialInstance / MaterialRenderProxy
    - shader reflection / parameter binding
    - concrete mesh pass / MeshPassDrawList / MeshDrawCommand
    - pipeline and binding cache
        |
        v
RHI frontend
    - RHIDevice
    - RHICommandContext / RHIGraphicsCommandContext
    - RHICommandList
    - RHIQueue
    - RHIViewportContext
    - RHI resources / views / descriptors
        |
        v
Backend
    - Vulkan
    - D3D11
    - D3D12
```

各层遵守以下边界：

- RenderScene 决定 pass、资源依赖、可见物体和绘制顺序。
- RenderCore 决定 shader permutation、material 参数、pipeline 描述和 draw packet。
- RHI 只表达跨 API 的资源、binding、命令、同步和提交语义。
- 后端负责原生对象、枚举转换、barrier、descriptor、command pool/list 和 fence。
- 上层禁止引用 `Vk*`、`ID3D11*`、`ID3D12*` 或按后端名称分支。

### 3.1 公共依赖与源码组织

RHI 公共层可以依赖 `engine/core/math` 中的纯值类型，例如 `vec2`、`vec3`、`vec4`、`uvec4` 和矩阵类型，避免在 RHI 内重复定义颜色、向量和矩阵表示。RHI 公共层不得依赖 GameScene、RenderScene、Material、窗口平台实现、Vulkan 或 Direct3D 头文件。

公共 RHI 按职责拆分，避免重新形成大型 `rhi_inilitializer.h`：

```text
rhi_result.h             error/status/result
rhi_types.h              enum、flags、基础值语义
rhi_capabilities.h       capability、limits、format support
rhi_descriptors.h        resource/view/shader/binding/pipeline descriptor
rhi_resource.h           公共资源身份和强引用
rhi_device.h             创建与 capability 查询
rhi_command_context.h    录制命令
rhi_queue.h              submit 与 queue completion value
rhi_viewport_context.h   frame 边界与 presentation
```

重构期间旧接口只作为待迁移代码，不得继续增加能力。每个旧类型必须明确映射到新类型或明确删除；调用方迁移完成后立即移除旧定义，禁止长期维护两套 `format`、`access`、clear value、resource 或 pipeline 模型。

## 4. RHI 公共对象

### 4.1 `RHIDevice`

`RHIDevice` 是 Renderer-owned、显式注入的唯一图形后端根门面。上层通过它完成初始化，查询 capability、limits 和 format support，创建长期 RHI object，并取得 graphics queue、viewport context 与 device-level graphics command context。它是 God facade，但不是 God implementation：draw-time transition、binding、render pass 与 draw 属于 `RHIGraphicsCommandContext`，submit/completion/wait 属于 `RHIQueue`，acquire/frame closure/present 属于 `RHIViewportContext` 与 `RHIFrameContext`。

全部公共 `create_*()` 使用 non-virtual interface。公共 frontend 依次执行 descriptor 结构检查、共享 lifecycle admission、initialized/terminal 检查、capability/limits/format support 与输入对象 owner identity 检查；只有通过后才恰好一次调用 protected `create_*_impl()`。backend hook 只处理 native mapping/allocation、API 前置条件与原生错误转换，不重复决定跨 API contract。成功结果必须携带当前 device identity；null 或错误 owner 作为 `BackendFailure`，不发布给上层。

```cpp
class RHIDevice
{
public:
    virtual ~RHIDevice() = default;

    virtual const RHICapabilities& capabilities() const = 0;
    virtual const RHILimits& limits() const = 0;

    RHIResult<RHIBufferRef> create_buffer(
        const RHIBufferDesc& desc,
        const RHIInitialData* initial_data = nullptr);

    RHIResult<RHITextureRef> create_texture(
        const RHITextureDesc& desc,
        const RHIInitialData* initial_data = nullptr);

    RHIResult<RHITextureViewRef> create_texture_view(
        const RHITextureRef& texture,
        const RHITextureViewDesc& desc);

    RHIResult<RHIShaderRef> create_shader(const RHIShaderDesc& desc);

    RHIResult<RHIBindingLayoutRef> create_binding_layout(
        const RHIBindingLayoutDesc& desc);

    RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline(
        const RHIGraphicsPipelineDesc& desc);

protected:
    virtual RHIResult<RHIBufferRef> create_buffer_impl(...) = 0;
    virtual RHIResult<RHITextureRef> create_texture_impl(...) = 0;
    virtual RHIResult<RHIGraphicsPipelineRef>
        create_graphics_pipeline_impl(...) = 0;
};
```

resource、view、shader、binding、pipeline、fence、viewport 和 device-level context 都保留不可变的非 owning owner device identity，仅用于公共层组合校验，不用于反向查找服务。跨 device 的 view、binding set 或 pipeline 必须在 native 调用前返回 `InvalidArgument`。公共头文件不得定义可变 `g_rhi`、singleton、service locator，或与 `RHIDevice` 平行的 `RHISystem`/`RHIManager` 创建入口；engine 初始化层显式拥有 device，并向 renderer 注入所需引用。

所有创建类别共享同一个 RAII admission。shutdown 先关闭 admission，拒绝新创建并等待在途创建离开，再执行 ordinary idle policy、清理 frontend cache 和 backend state；`shutdown_after_device_lost()` 复用相同 admission，但不再次调用 native idle wait。descriptor/能力错误、`NotReady`、`DeviceLost`、`Unsupported`、`OutOfMemory` 与 `BackendFailure` 必须保持可诊断分类。

#### 4.1.1 Vulkan backend implementation 边界

`VulkanDevice` 是 `RHIDevice` 的唯一 Vulkan facade、native device 生命周期 owner 和 backend composition root。它拥有 instance、primary surface、physical/logical device，以及 queue、memory、upload 和 deferred-deletion 服务；上层与 renderscene 不得持有其他 Vulkan 根对象。

`VulkanDevice` 不作为 backend 内部 service locator。RHI→Vulkan 转换集中在 `vulkan_type_mapping.*`；resource/view/shader/sampler、binding layout/set/physical packet 和 graphics pipeline 的原生创建分别位于窄 creation modules。`VulkanDevice::create_*_impl()` 只传入 owner identity、native handle、descriptor 与所需 manager，并原样返回结果。creation module 不接收 frontend `initialized` 状态、不调用公共 `create_*()`，也不重复 lifecycle、capability 或公共 descriptor policy；需要原生 handle 的函数只防御 `VK_NULL_HANDLE` 等 Vulkan API 前置条件。

`VulkanGraphicsCommandContext`、`VulkanViewportContext` 和 `VulkanSwapchain` 通过构造函数逐项获得实际依赖，不保存 `VulkanDevice&`，也不通过 device accessor 查找服务。逐项注入的 owner identity 只用于跨对象归属校验，不能反向取得 device 服务；所有 native handle 和 non-owning service reference 的生命周期都严格短于 owning `VulkanDevice`。不得用 `VulkanDeviceServices`、第二个 device wrapper、static mutable state 或新旧双轨 creation path 缩短参数列表。

### 4.2 Command context 与 command list

创建和执行必须分离。公共 command context 分为通用、graphics 和预留 compute 三层：

```cpp
class RHICommandContext
{
public:
    virtual ~RHICommandContext() = default;

    virtual RHIResult begin_recording() = 0;
    virtual RHIResult transition_resources(
        Span<const RHIResourceTransition> transitions) = 0;
    virtual RHIResult copy_buffer(const RHIBufferCopyDesc& desc) = 0;
    virtual RHIResult copy_texture(const RHITextureCopyDesc& desc) = 0;
    virtual RHIResult<RHICommandListRef> finish_recording() = 0;
};

class RHIGraphicsCommandContext : public RHICommandContext
{
public:
    virtual RHIResult begin_render_pass(
        const RHIRenderPassDesc& desc) = 0;
    virtual RHIResult end_render_pass() = 0;

    virtual RHIResult set_graphics_pipeline(
        const RHIGraphicsPipelineRef& pipeline) = 0;
    virtual RHIResult set_viewport(const RHIViewport& viewport) = 0;
    virtual RHIResult set_scissor(const RHIRect& rect) = 0;
    virtual RHIResult bind_graphics_resources(
        const RHIGraphicsBindings& bindings) = 0;
    virtual RHIResult draw(const RHIDrawArgs& args) = 0;
    virtual RHIResult draw_indexed(const RHIDrawIndexedArgs& args) = 0;
};
```

`RHICommandList` 是结束后不可修改的公共录制单元，不等同于 Vulkan `VkCommandBuffer` 或 D3D12 `ID3D12GraphicsCommandList`。第一阶段后端可以串行复用一个 immediate graphics context，但上层接口不得假定 context 永远唯一。

### 4.3 Queue

```cpp
class RHIQueue
{
public:
    virtual RHIResult<RHIQueueCompletionValue> submit(
        Span<const RHICommandListRef> command_lists,
        const RHISubmitInfo& submit_info) = 0;

    virtual RHIQueueCompletionValue completed_value() const = 0;
    virtual RHIResult wait_for_value(RHIQueueCompletionValue value) = 0;
};
```

`RHIQueueCompletionValue` 是 deferred deletion、frame resource、descriptor pool、command pool 和 upload ring 回收的统一完成依据。它只在所属 queue 内单调递增和可比较。

### 4.4 Viewport 与 presentation

```cpp
class RHIViewportContext
{
public:
    virtual RHIResult<std::unique_ptr<RHIFrameContext>> begin_frame() = 0;
    virtual RHIResult<RHIFrameEndResult> end_frame(
        std::unique_ptr<RHIFrameContext> frame,
        const std::vector<RHICommandListRef>& command_lists) = 0;
    virtual RHIStatus abort_frame(std::unique_ptr<RHIFrameContext> frame) = 0;
    virtual RHIStatus request_resize(uint32 width, uint32 height) = 0;
};
```

`RHIFrameEndResult` 将业务提交事实与 presentation 结果分开：外层成功表示业务 command list 已提交，
并携带有效 `completion_value`；`presentation_status` 独立表达 Success、Suboptimal、OutOfDate 或 terminal。
submit 成功后，present 的任何结果都不得回滚已经发布的资源状态或 in-flight ownership。

公共 queue completion 与后端 presentation completion 是两个不同的完成域。`RHIQueueCompletionValue`
或 Vulkan submit fence 只证明 graphics submission 和其捕获的 command list、resource、descriptor、upload
payload 已完成，不能单独证明 WSI 已消费 present wait semaphore。Vulkan backend 使用
`VulkanViewportContext + VulkanSwapchain` 作为唯一 presentation owner 链：viewport 管理公共 frame 编排、
`VulkanFrameSlot`、submit/abort、resize pending 与 terminal status；swapchain 管理 `VkSwapchainKHR`、
`VulkanSwapchainImage`、acquire 和 present。`VulkanFrameSlot` 拥有 `image_acquired` semaphore、
`submission_fence`、command pool、present-transition command buffer 与提交 payload；每个
`VulkanSwapchainImage` 拥有 image/view、RHI texture/view 与 `rendering_done` semaphore，并以 non-owning
`last_submission_fence` 记录上次 graphics 使用。frame slot index、swapchain image index、逻辑 frame id
和 queue completion value 禁止互换，slot 数固定为 `min(2, actual_image_count)`。

Vulkan ES3.1 profile 不要求 WSI completion extension。正常运行中，成功 present 后只有同一 swapchain image
再次成功 acquire，才能证明该 image 的 `rendering_done` semaphore 可复用；submit fence signal 本身不足以
证明 WSI completion。resize、`Suboptimal` 或 `OutOfDate` 只在没有 active acquired frame 的后续
`begin_frame()` 边界重建：zero extent 返回 `NotReady`；其余路径先等待当前 shared graphics/present queue
idle，再在局部 `unique_ptr` 中完整构造 replacement，成功后才发布并销毁旧 swapchain。常规 recreate
不得调用 `vkDeviceWaitIdle()`；该调用只保留给 shutdown 或 terminal cleanup。第一阶段删除
`VK_EXT_swapchain_maintenance1`、present fence、retired generations 与异步 retirement；未来若引入独立
present queue，必须重新设计为等待所有使用旧 swapchain 的 graphics/present queues。

Swapchain 是各后端 `RHIViewportContext` 的内部 presentation 组件，不建立公共 `RHISwapchain` 或 `RHIDevice::create_swapchain()` 平行路径。`begin_frame()` 返回当前 presentation texture/view，`end_frame()` 统一完成 submit 和 present；image index、frame slot、acquire/present 同步对象及原生 swapchain 均不得泄漏到 renderscene。Out-of-date、suboptimal、surface lost、device lost 和延迟 resize 由 viewport 内部处理并通过可诊断结果反馈。

`RHISurface` 只携带 platform kind 与不透明平台 presentation handle。平台层必须在窗口所属线程完成原生窗口对象及其 presentation layer 的准备；backend 只消费已准备的 handle 创建图形 API surface。macOS 的 `CAMetalLayer` 由 `MacWindow` 在 main thread 挂接，Vulkan Rendering Thread 只调用 `vkCreateMetalSurfaceEXT`，不得通过后台线程修改 `NSView`/`CALayer`。Win32 的 `HWND` 创建与消息处理仍属于 main thread，而 `vkCreateWin32SurfaceKHR` 可在 Rendering Thread 消费该稳定 handle。

viewport status 中 `NotReady`、`OutOfDate` 与 `Suboptimal` 分别表达暂时无可用 extent、需要重建、
以及本帧完成但后续应重建，caller 可将它们作为 recoverable frame outcome。不可恢复的 submit、
同步或 surface failure 保持 `DeviceLost`、`BackendFailure` 等原始诊断，不得降格为 `NotReady`
而被渲染循环永久静默忽略。

`begin_frame()` 的 acquire 返回 `OutOfDate` 时，本次直接返回可恢复结果并保持 resize pending；
不得在重建 swapchain 后继续使用重建前 frame slot 的 semaphore/fence 引用重试 acquire。acquire 已成功但
recording 无法继续时，caller 必须调用 `abort_frame()`；backend 只提交恢复 present layout 所需的最小
command buffer，不提交已丢弃的业务 command list，并消费 acquire synchronization、尝试 present、推进
frame slot。若最小提交、同步或 present 前的恢复步骤失败，本帧不再具备可重试语义：即使底层返回
`NotReady`、`OutOfDate` 或 `Suboptimal`，也必须提升为 terminal `BackendFailure` 并锁存，禁止后续帧复用
状态未知的 presentation object。若 GPU submit 已成功，后续 CPU 状态发布异常也仍须尝试 present，以消费
已经安排 signal 的 presentation semaphore，再报告 terminal failure。

## 5. 资源与 view

### 5.0 共享 PixelFormat contract

Toy3d 只有一套 GPU-ready storage format：`engine/core/pixel_format/pixel_format.h` 中的 `PixelFormat`，对应 UE4.27 `EPixelFormat` 的职责。它由独立 `Toy3dPixelFormat` target 提供，供 runtime、editor、tools 和公共 RHI 共同依赖；公共 RHI 不再定义 `RHIFormat`，Asset/Editor 也不得为格式身份反向依赖 RHI。

```text
外部文件编码 / Editor source
        ↓ import / cook
PixelFormat + GPU-ready mip payload
        ├─ Editor preview
        ├─ Cook output / runtime TextureDesc
        └─ RHI resource/view/pipeline descriptors
                ↓ backend-local conversion
          VkFormat / DXGI_FORMAT
```

第一阶段非目标包括 PNG/JPEG/DDS 解析、可重新 Cook source data、import/color policy 和独立 `TextureSourceFormat`。这些属于未来 Editor/Asset source layer；共享 Core 不反向依赖 Asset、RenderScene、RHI 或 backend。

`PixelFormat` 是无 ownership、无生命周期和无线程可变状态的 `enum class`。共享实现只提供 block width、block height、bytes per block 与最小 row/slice pitch checked calculation；Unknown/Max、零 extent 或溢出返回无可用 metadata/`false`，调用方负责增加 asset、mip 和 target 上下文。BC、ASTC、PVRTC 等格式必须按向上取整的 block count 计算 pitch，禁止 bytes-per-texel 近似。

平台差异不进入枚举：Cook profile 与 runtime format capabilities 验证完整 usage/sample 组合；Vulkan、D3D11 FL11_0 和 D3D12 backend 分别显式 switch 到 `VkFormat`/`DXGI_FORMAT`，未知或不支持映射返回 `Unsupported`，禁止数字强转。公共头文件不得出现 native format 类型。

测试矩阵包括共享 metadata 与非 block-aligned pitch、公共 RHI format capability/descriptor validation、各 backend mapping/support，以及 Editor/Cook→runtime payload contract。迁移采用单批次切换：全部公共 descriptor、RenderCore 和 backend 调用点改用 `PixelFormat` 后立即删除 `RHIFormat`；删除条件是无 compatibility alias、数字强转或第二套 GPU-ready format，且 configure、受影响 targets 与定向测试通过。

### 5.1 Descriptor

Buffer 和 texture 使用完整 descriptor 创建。Descriptor 至少表达：

- dimension、extent、format、mip、array layer 和 sample count；
- usage、CPU access 和 initial access；
- debug name；
- buffer size、stride 和结构化元素信息；
- texture clear value；
- 非法组合的创建前验证。

Initial data 必须包含 size、row pitch、slice pitch、所有权和消费时机。禁止只传裸 `void*` 并假定后端知道数据大小。

### 5.2 独立 view

Texture/buffer 与其用途 view 分离：

```text
RHITexture
    - RHIShaderResourceView
    - RHIUnorderedAccessView
    - RHIRenderTargetView
    - RHIDepthStencilView

RHIBuffer
    - RHIShaderResourceView
    - RHIUnorderedAccessView
```

View descriptor 表达 format、mip/layer 范围、aspect 和 depth/stencil read-only 属性。Render pass 只能引用 RTV/DSV view，不直接引用 texture。

公共 view format 表达引擎语义而不是原生存储格式。`D24UNormS8UInt` texture 的 DSV 选择
`DepthStencil` aspect，供 Shader 读取的 SRV 保持相同公共 format 并只选择 `Depth` aspect；
D3D11/D3D12 使用 typeless resource 及不同原生 DSV/SRV format 的细节由 backend 封装。
texture 创建必须验证 format capability 覆盖所请求的全部 usage 与 sample count；第一版 Renderer
要求 `R16G16B16A16Float` 支持 `RenderTarget | ShaderResource`，并要求 `D24UNormS8UInt`
支持 `DepthStencil | ShaderResource`。缺少任一能力必须返回可诊断的 `Unsupported`。

后端映射如下：

| 公共对象 | Vulkan | D3D11 | D3D12 |
|---|---|---|---|
| texture view | `VkImageView` | SRV/UAV/RTV/DSV | view descriptor |
| buffer view | buffer descriptor | SRV/UAV | view descriptor |
| render target view | color `VkImageView` | RTV | RTV descriptor |
| depth stencil view | depth/stencil `VkImageView` | DSV | DSV descriptor |

## 6. 资源状态与同步

公共层使用 `ERHIAccess` 表达用途，不暴露 Vulkan image layout、pipeline stage/access mask 或 D3D12 resource state。

```cpp
struct RHIResourceTransition
{
    RHIResourceRef resource;
    RHISubresourceRange subresources;
    ERHIAccess before;
    ERHIAccess after;
};
```

三后端处理方式：

- Vulkan：生成 pipeline barrier、access mask 和 image layout transition；
- D3D12：生成 resource barrier；
- D3D11：跟踪逻辑状态、验证 hazard，并解除 SRV/RTV/DSV/UAV 冲突绑定。

资源对象不得私自 submit、wait idle 或执行 immediate transition。Upload、copy 和 transition 必须记录到 command context，由 queue 统一提交。

Command list 的 tracker 对每个 transition 或实际 resource usage 记录 first required access、当前 local access 和 final access；录制期间只推进 local state。Queue 在同一提交临界区内按 `validate current committed → native submit → publish final committed` 的顺序处理，因此 committed state 以原生 queue 的实际成功 submit 顺序为准，而不是 command list 的录制顺序。失败且明确未产生 GPU work 时不发布 final state。

每个由 device 创建的 `RHIObject` 保存不可变的创建 device identity。Resource、view、shader、binding、pipeline、render-pass attachment 和 command list 在创建、录制或 submit 入口先验证 identity，再执行 backend downcast 或原生 API；platform surface 是 device 创建前存在的例外，不伪造 device owner。Device 必须晚于其全部子对象销毁。

## 7. 生命周期

资源生命周期按以下模型实现：

1. `RHIDevice` 创建公共资源。
2. Render/Material/scene 对资源持有公共强引用。
3. Command list 在录制期间保留 GPU 工作所需资源引用。
4. Submit 后，command list 和资源引用与本次提交返回的 `completion_value` 绑定。
5. CPU 最后一个引用释放后，原生对象进入 deferred-deletion queue。
6. 仅当 `completed_value >= retire_value` 时销毁原生对象。
7. Device 晚于所有子资源、swapchain、pool 和 cache 销毁。

Descriptor pool、command pool、upload ring 和临时 framebuffer 按 frame-in-flight/completion value 分代，GPU 完成前不得 reset 或复用。

Completion 只控制 GPU payload 回收：command list、staging page、descriptor packet、pipeline/binding/resource 强引用和临时 render-pass payload 都保留到对应 `RHIQueueCompletionValue` 完成。Vulkan 使用 submit fence；D3D12 使用 fence value；D3D11 FL11_0 基线在 `ExecuteCommandList` 后发出 `D3D11_QUERY_EVENT`，以 `ID3D11DeviceContext::GetData` 确认 GPU 到达该点。`ExecuteCommandList` 的 CPU 返回不得视为 completion，且不依赖 D3D11.3 fence 抬高基线。

## 8. Shader 系统边界

### 8.1 RHI Shader

RHI shader descriptor 只包含后端创建 shader 所需的信息：

- shader stage；
- 目标 bytecode；
- entry point；
- reflection/binding metadata；
- 稳定 content hash；
- debug name。

Shader 源文件、include、permutation、Material 和 GlobalShader 类型都属于 RenderCore，不进入 RHI。

### 8.2 GlobalShader

GlobalShader 适用于不依赖具体 Material/VertexFactory 的渲染任务：

- fullscreen triangle；
- clear、copy 和 blit；
- tone mapping 和后处理；
- debug rendering；
- mip generation；
- compute utilities。

GlobalShader 的流程参考 UE4.27：

```text
GlobalShader 类型注册
        |
        v
platform / feature / permutation 编译
        |
        v
GlobalShaderMap 缓存编译结果
        |
        v
Render pass 获取 typed shader
        |
        v
RenderCore 获取 pipeline 和 binding
        |
        v
RHI command context draw / dispatch
```

建议的稳定 key 包含 shader type、permutation、target、feature level 和 compilation environment hash。第一阶段使用显式 registry 即可，不要求复制 UE 的静态注册宏。

### 8.3 MaterialShader

BasePass、DepthPass、ShadowPass 等 mesh pass 使用 MaterialShader，而不是把所有 shader 放入 GlobalShaderMap。

Material shader variant 的选择至少依赖：

- Material 的 shader identity 与静态属性；
- static switches；
- VertexFactory type；
- MeshPass type；
- feature level；
- shader target。

GlobalShader 和 MaterialShader 最终都生成同一种 `RHIShader`，差异只存在于 RenderCore 的注册、编译和缓存层。

## 9. Shader 参数域

参数按所有者和更新频率分域，禁止把所有参数保存为无归属的扁平 slot 表。

| 参数域 | 内容示例 | 更新频率 | 所有者 |
|---|---|---|---|
| Global | 时间、环境和全局 sampler | frame 或更低 | Render system |
| View | view/projection、camera、viewport | 每个 view | Scene renderer |
| Pass | light、shadow、GBuffer 和 pass texture | 每个 pass | Render pass |
| Material | base color、roughness 和材质 texture | 每个 material instance | Material system |
| Object | model matrix、primitive ID、skin data | draw/instance | Primitive/draw packet |

```cpp
enum class ShaderParameterScope : uint8_t
{
    Global,
    View,
    Pass,
    Material,
    Object
};

struct ShaderParameterId
{
    uint64 value;
};
```

`ShaderParameterId` 使用 Shader 规范锁定的 64-bit FNV-1a 与带长度字段输入编码，由 binding group、category 和 parameter name 生成；0 为 invalid。资产和 MaterialInstance 按稳定 ID 存储数据，并保留原始名字用于诊断。`binding_slot`、constant buffer offset 和后端 descriptor 位置只能作为 shader 编译/reflection 的派生结果，不能作为资产格式中的持久标识。

Constant 数据按更新频率分块：

```text
GlobalConstants
ViewConstants
PassConstants
MaterialConstants
ObjectConstants
```

Global/View/Pass/Object 可使用 frame upload allocator；Material 保存持久 CPU 参数数据，值变更后按版本上传。Vulkan/D3D12 可使用 dynamic uniform/constant buffer offset 或 upload buffer，D3D11 后端使用 dynamic constant buffer 和安全的 discard/suballocation 策略。

## 10. Binding layout 与 binding set

Global/View physical binding 聚合的公共接口、后端 materialization、生命周期、错误语义、
测试矩阵和迁移删除条件详见 `rhi-binding-aggregation-design.md`。本节保留长期分层原则；若
实现细节存在歧义，以该专项设计的已确认 contract 为准。

公共 binding layout 使用 shader 可见的资源语义，不暴露 descriptor set、descriptor heap 或 root parameter：

```cpp
enum class RHIBindingGroup : uint8_t
{
    Global,
    View,
    Pass,
    Material,
    Object
};

struct RHIBindingLayoutEntry
{
    ShaderParameterId binding_id;
    RHIBindingGroup group;
    uint32 target_binding;
    RHIResourceBindingType type;
    RHIShaderStageFlags stages;
    uint32 array_count;
    uint32 data_size;
    ShaderDataLayoutHash data_layout_hash;
    uint32 shader_abi_version;
};
```

Binding resource type 至少包括 uniform buffer、sampled texture、storage texture、sampler、storage buffer。Compute 和 storage binding 可从第一版进入 descriptor，但实际调用受 capability 控制。

同一逻辑参数在不同 target/stage 的 native mapping由 Shader产物明确记录。`binding_id`与完整
constant data ABI属于跨 target逻辑身份；`target_binding`只属于当前 target mapping。

```cpp
struct RHIGraphicsBindings
{
    RHIBindingSetRef global;
    RHIBindingSetRef view;
    RHIBindingSetRef pass;
    RHIBindingSetRef material;
    RHIBindingSetRef object;
};
```

映射规则：

- Vulkan backend 将多个 logical group 按 profile 打包到 physical descriptor sets；logical group 与 descriptor set 不一一对应。`Vulkan ES3.1 profile` 固定 set 0=Global+View、set 1=Pass、set 2=Material、set 3=Object；
- D3D12 可编译为 descriptor table、root CBV 或 root constants；
- D3D11 展开为各 shader stage 的 CBV/SRV/UAV/sampler slot。

`target_binding` 是当前 Shader target record、当前 stage 已编译的 RHI 位置，不是跨 target 的资产级 slot。公共枚举数值不等于 Vulkan set index 或 D3D12 root parameter index。映射只存在于 target-specific binding layout 编译结果中。

`RHIBindingSet` 是按稳定ID索引、与 Program layout解耦的单 group immutable snapshot。draw flush
使用当前 Pipeline layout解析 active values；Vulkan根据解析结果构造 set 0..3，并以dynamic uniform
offset复用同一backing page上的descriptor packet。command list只保活active资源、native packet、
uniform/descriptor pages直到queue completion。Pass不得修改Material binding，Material也不得持有
SceneColor、SceneDepth等pass resource。

## 11. Material 系统预留

Material 系统正式采用：

```text
Material → MaterialInstance → MaterialRenderProxy
```

不引入 `MaterialTemplate` 或 `MaterialInterface`。当前 Material 线程与资源边界见 OpenSpec change
`establish-game-render-framework` 的 `game-render-framework/material-updates` 与 `game-render-framework/render-resource-manager` capability。

### 11.1 `Material`

`Material` 是不可变资产定义，保存稳定参数 schema、静态渲染属性与 ShaderMap
reference。第一版静态属性包括 Phong shading model、`Opaque | Translucent` blend
mode 与 `two_sided`。静态属性参与 shader/PSO 选择，不作为普通 dynamic parameter
上传。

Game 侧共享引用为 `shared_ptr<const Material>`。Material 不保存 RHI object、Vulkan
descriptor set 或 D3D12 descriptor handle。

### 11.2 `MaterialInstance`

`MaterialInstance` 保存 `MaterialRef` 与由稳定 `ShaderParameterId` 标识的 typed
parameter override。参数类型必须匹配 Material schema；普通动态参数更新按 RenderCommand FIFO
更新稳定 `MaterialRenderProxy`，不携带通用 revision，也不触发 shader 编译。`StaticMeshComponent` 始终引用 MaterialInstance，不在 Component
内复制一套材质字段。

### 11.3 `MaterialRenderProxy`

`MaterialInstance` 拥有地址稳定的 `MaterialRenderProxy` render representation，其可变状态只由
Render Thread 访问。Proxy 保存 RT 参数表、non-owning `TextureRenderResource*`、ShaderMap program
和 binding cache；Draw 前解析 override/default 并按需物化 frame-local constants 与 Material binding。
Texture Asset/上层状态保证引用生命周期，已录制 GPU 工作则由 RHI command list 强引用实际 view、
binding 与 native resource，直至对应 queue completion。

### 11.4 稳定 Binding ABI

为避免 Vulkan pipeline layout 和 D3D12 root signature 碎片化：

- Global/View/Pass group 由 renderer 约定稳定布局；
- Material group 由 Material 参数 schema 与 shader reflection 共同定义；
- Object group 由 renderer/VertexFactory 定义；
- 同一 Material 的 dynamic parameter ABI 应跨兼容 shader variant 保持稳定；
- reflection 必须与 schema 做完整验证；
- layout cache 使用完整稳定 key，hash 命中后继续做 equality 比较。

## 12. Pipeline

Graphics pipeline descriptor 是完整不可变值，至少覆盖：

- shader stages；
- binding layout；
- vertex input layout；
- primitive topology；
- rasterizer、blend 和 depth/stencil state；
- color attachment formats；
- depth/stencil format；
- sample count；
- dynamic state mask。

Vulkan 和 D3D12 创建原生 pipeline/PSO；D3D11 将其编译为 shader 与 state object 的不可变组合。公共接口不暴露 pipeline layout 或 root signature。

Pipeline cache key 不得使用对象地址，必须覆盖完整 descriptor 和 shader content hash；hash collision 后必须比较完整 descriptor。

Pipeline 创建入口采用 non-virtual interface。公共 `RHIDevice::create_graphics_pipeline()` 负责 validation、capability/limits、descriptor canonicalization、完整 key 和并发 cache；各后端只实现受保护的 `create_graphics_pipeline_impl()`。公共 cache 由 device 拥有；shutdown 通过 lifecycle gate 拒绝新创建、等待已进入创建结束，再等待 GPU idle、释放 cache 并销毁 native device。Vulkan/D3D12 的 native cache blob 或 pipeline library 属于后端第二级 cache，不能代替公共逻辑 PSO cache。

## 13. Render pass 与 Render 上层调用

### 13.1 Pass 职责

当前显式 Renderer 的具体业务 Pass 使用单一 `render_*_pass(...)` 入口，并在入口内部保持
prepare-then-execute 边界。prepare 阶段可以显式使用 Renderer 注入的 `RHIDevice&`、
`RHIShaderProgramCache&` 与当前 `RHIGraphicsCommandContext&`：它读取当前帧 View candidates，
创建或查询 shader、pipeline、binding，并在同一个 recording 中完成 draw 所需的 uniform
upload。所有这些工作必须在 `begin_render_pass()` 前完成。

```cpp
RHIStatus render_base_pass(
    RHIDevice& device,
    RHIShaderProgramCache& shader_program_cache,
    RHIGraphicsCommandContext& context,
    const BasePassInputs& inputs);
```

prepare 的帧内结果由局部 `MeshPassDrawList`/`MeshDrawCommand` 保存 dynamic state、pipeline、
vertex/index binding、完整 `RHIGraphicsBindings` snapshot 和 draw arguments 等值与 RHI 强引用；
不得保存 attachment、device、queue、frame context、RenderScene、Material、Proxy 或 MeshBatch
指针，也不得跨帧缓存。具体 Pass 自己持有 attachment/load/store/clear contract，在全部 draw
command 准备成功后 begin/end RHI render pass。render-pass scope 内只设置已准备状态并 draw，
不得回读准备源或调用任何 device creation。

第一阶段由 Renderer frame orchestration 显式决定 viewport acquisition、pending upload、最终输出
transition、finish 和 frame closure，仍保持单 viewport、单 graphics context、单 immutable business
command list。`SceneRenderer` 只在注入的同一 context 中执行 View/visibility、View uniform prepare 和 scene pass
recording，不拥有一次性UI payload，也不调用Tonemap、ImGui、present或submit。任一阶段失败由外层
frame owner discard 当前 RenderResource recording并调用 `abort_frame()`。当前不建立通用
`RenderPass` 基类、Pass Scheduler或command packet hierarchy；后续RDG位于renderscene，形成真实
资源依赖后再统一承担声明、调度和barrier规划。

### 13.2 GlobalShader pass

GlobalShader pass 适合 test/fullscreen/compute 类任务：

```cpp
RHIResult TestPass::execute(RenderPassContext& context)
{
    const auto vertex_shader =
        context.global_shader_map.get_shader<TestVertexShader>();
    const auto pixel_shader =
        context.global_shader_map.get_shader<TestPixelShader>();

    const auto pipeline = context.pipeline_cache.get_or_create(
        make_test_pipeline_desc(
            vertex_shader,
            pixel_shader,
            context.attachment_formats));

    RHIGraphicsCommandContext& commands = context.graphics_context;
    RHI_TRY(commands.begin_render_pass(context.render_pass_desc));
    RHI_TRY(commands.set_graphics_pipeline(pipeline));
    RHI_TRY(commands.bind_graphics_resources(context.graphics_bindings));
    RHI_TRY(commands.draw({3, 1, 0, 0}));
    return commands.end_render_pass();
}
```

### 13.3 BasePass

BasePass 参考 UE4.27 的职责分层，但使用无状态具体入口：

```text
Visible primitives
        |
        v
MeshBatch
        |
        v
render_base_pass(...)
    - pass eligibility
    - material fallback
    - shader variant
    - render state
    - sort key
        |
        v
MeshPassDrawList / MeshDrawCommand
    - pipeline
    - vertex/index buffers
    - material/object bindings
    - draw arguments
        |
        v
sort / filter / optional merge
        |
        v
RHIGraphicsCommandContext
```

BasePass 位于 `engine/runtime/renderscene/pass/base_pass.*`。它根据 Material shader identity、
effective graphics state 与 LocalVertexFactory 获取 shader/pipeline/binding，并构建当前帧 draw
list。第一阶段不新增通用 `MeshPassProcessor` 基类；ShadowPass 等具体 pass 落地并形成真实重复
后再评估普通 helper 或 processor abstraction。

### 13.4 `MeshPassDrawList` 与 `MeshDrawCommand`

```cpp
struct MeshDrawCommand
{
    RHIGraphicsPipelineRef pipeline;
    std::vector<RHIVertexBufferBinding> vertex_buffers;
    RHIIndexBufferBinding index_buffer;
    RHIGraphicsBindings bindings;
    RHIDrawIndexedArgs draw_args;
    std::uint64_t sort_key = 0;
};

struct MeshPassDrawList
{
    RHIViewport viewport;
    RHIRect scissor;
    std::vector<MeshDrawCommand> commands;
};
```

Global/View/Pass、Material/Object 按各自 owner 和更新频率准备；每个 command 保存完整 logical
binding snapshot。同一 View 的 canonical bytes、transient uniform slice 与 logical BindingSet
只准备一次；不同 Pipeline target mapping 在 draw flush中按稳定ID解析，不创建layout adapter：

```cpp
for (const MeshPassDrawList& draw_list : draw_lists)
{
    for (const MeshDrawCommand& command : draw_list.commands)
    {
        commands.set_graphics_pipeline(command.pipeline);
        commands.set_viewport(draw_list.viewport);
        commands.set_scissor(draw_list.scissor);
        commands.bind_graphics_bindings(command.bindings);
        commands.draw_indexed(command.draw_args);
    }
}
```

提交器可缓存当前 pipeline、vertex stream 和 binding set，减少重复 RHI 命令。缓存属于 command recording/RenderCore，不改变 draw command 的语义。

## 14. Render 与 RHI 的禁止依赖

以下类型或概念不得进入公共 RHI：

- `GlobalShader`、`MaterialShader` 和 shader permutation domain；
- `Material`、`MaterialInstance`、`MaterialRenderProxy` 和 Material 参数资产；
- `VertexFactory`、`MeshBatch` 和 `MeshPassProcessor`；
- `BasePass`、`ShadowPass` 和 mesh sorting；
- shader source compilation 和 include 管理；
- Render Graph 的 pass dependency 对象。

RHI 只接收这些类型编译后的结果：shader bytecode、binding layout/set、pipeline descriptor、resource/view、render pass descriptor、command 和 submit dependency。

## 15. Capability、错误和线程

`RHICapabilities` 和 `RHILimits` 至少覆盖：

- graphics、compute、storage resource、indirect draw；
- geometry/tessellation shader；
- format usage、MSAA sample count；
- buffer/texture alignment；
- attachment、binding slot、dimension limits；
- timestamp query 和 async compute；
- 后端可支持的 recording 并行度。

### 15.1 Platform Profile 与移动端边界

Profile 是版本化的离线编译和验证基线，不等于 backend。平台配置选择默认 profile；runtime 仍以实际 `RHICapabilities`、`RHILimits` 和 format support 复核 ShaderPackage 的 requirements。默认 `Vulkan ES3.1 profile` 固定为 Vulkan 1.1、SPIR-V 1.3、最多四个 bound descriptor sets，且不默认依赖可选 device feature。

高于 portable 基线的功能必须由独立 profile 或 `Requires <Capability>` 显式声明，禁止根据当前桌面 GPU 自动提高 Cook 输出要求。Cook 和 runtime 都验证 sampler、sampled image、uniform/storage buffer、storage image 的 per-stage 与 pipeline-layout limits；错误需报告 group、stage、resource class、required 和 supported。ShaderPackage/ShaderCodeLibrary 保存 required capabilities/limits，不兼容时返回可诊断的 `UnsupportedCapability`。

### 15.2 统一图形约定

RHI pipeline 与 viewport 映射遵守 Shader 系统的统一约定：left-handed、+Z forward、column-vector、column-major、clip depth 0..1、reversed-Z（clear 0.0、默认 `GreaterEqual`）和公共 CounterClockwise front face。Vulkan 1.1 backend 使用 negative viewport height 处理 Y 时，必须同步修正 native front-face mapping；上层和 Shader 不做 backend-specific Y flip。

所有初始化、创建、map/update、acquire、submit、present 和 resize 操作返回可检查结果。Assert 只用于内部不变量，不能替代 Release 错误路径。

`NotReady`、`OutOfDate` 与 `Suboptimal` 是可恢复 viewport status，创建这些 status 时不得记录为
RHI error：`NotReady` 使用 debug，`OutOfDate`/`Suboptimal` 使用 info。`DeviceLost`、
`BackendFailure`、`OutOfMemory` 及调用 contract 错误继续记录 error。日志级别不得改变
`RHIStatus` 返回值、恢复判断或 submit/present 分离语义。

第一阶段规定：

- renderer/render thread 拥有 device、queue 和 swapchain 的主要调用权；
- 每个 recording context 在录制期间只属于一个线程；
- 结束后的 command list 不可修改，可跨线程交给 queue；
- 资源对象对只读操作线程安全，可变后端状态必须 context-local 或内部同步；
- device lost 后停止继续调用原生 API，并进入统一终止或恢复路径。

## 16. 三后端实现映射

| 公共语义 | Vulkan | D3D11 | D3D12 |
|---|---|---|---|
| resource access | barrier/layout | logic state + unbind hazard | resource state barrier |
| graphics pipeline | pipeline + layout | shader/state object 组合 | PSO + root signature |
| binding layout | descriptor set layout | stage slot layout | root signature/table |
| binding set | descriptor set | CBV/SRV/UAV/sampler binding packet | descriptor table/root binding |
| render pass | dynamic rendering/render pass | OM + clear/resolve | render pass API 或命令组合 |
| recording | command buffer | deferred/immediate context | command list |
| submit completion | submit fence/timeline | FL11_0 `D3D11_QUERY_EVENT` | fence value |
| async compute | capability gated | 通常不作为基线 | capability gated |
| device creation frontend | NVI validation + Vulkan hook | 同一 NVI + FL11_0 hook | 同一 NVI + D3D12 hook |
| unsupported creation | `Unsupported`，不进入无效 native path | `Unsupported`，不得空操作成功 | `Unsupported`，不得发布空壳对象 |

D3D11 后端不能提供与 Vulkan/D3D12 等价的显式 barrier 或并行能力时，可以安全退化为串行和隐式同步，但渲染结果与公共错误语义必须一致。

## 17. 迁移与实现顺序

### 阶段 1：公共定义

- 统一 Vulkan、D3D11、D3D12 目标；
- 建立 `RHIResult` 和错误分类；
- 重构 enum、descriptor、capability 和 limits；
- 定义 resource、view、subresource range 和 initial data；
- 定义 shader、binding layout/set 和完整 pipeline descriptor。

### 阶段 2：公共接口拆分

- 建立 `RHIDevice`；
- 建立 `RHICommandContext` 和 `RHIGraphicsCommandContext`；
- 建立 `RHICommandList`、`RHIQueue` 和 `RHIViewportContext`，将 swapchain 收入 viewport 后端实现；
- Vulkan 后端直接实现公共 `RHIDevice` 接口，不保留旧 `IDynamicRHI` adapter；
- engine 初始化层显式持有并注入 RHI device，不定义可变全局 `g_rhi`。

### 阶段 3：RenderCore 基础

- 建立 shader reflection 和稳定 content hash；
- 建立 Global/View/Pass/Material/Object 参数域；
- 建立 `GlobalShaderMap`；
- 建立 pipeline cache 和 binding cache；
- 迁移 test pass 到 GlobalShader 路径。

### 阶段 4：Vulkan graphics 闭环

- swapchain acquire/present/resize；
- buffer、texture 和 view；
- upload/copy/transition；
- graphics pipeline 和 binding；
- render pass 和 draw；
- queue completion value、frame resource 和 deferred deletion；
- 通过 validation layer 验证多 frame-in-flight。

### 阶段 5：Material 与 BasePass 骨架

- 建立最小 `Material`、`MaterialInstance` 与 `MaterialRenderProxy`；
- 区分 static/dynamic parameters；
- 建立 `MaterialShaderVariant` 和 `MaterialShaderMap`；
- 建立 `MeshBatch`、具体 BasePass、`MeshPassDrawList` 和 `MeshDrawCommand`；
- 完成 SceneColor/SceneDepth 的 BasePass 绘制闭环。

### 阶段 6：跨后端审计

- 对每个公共 descriptor 和命令写出 D3D11/D3D12 映射；
- 检查是否存在 Vulkan descriptor set、layout 或 stage mask 泄漏；
- 对 D3D11 不支持能力提供 capability/`Unsupported` 路径；
- 使用后端独立测试覆盖创建、binding、transition、draw 和 resize。

### 阶段 7：后续演进

- 先按 OpenSpec change `establish-game-render-framework` 完成显式 SceneRenderer 与资源生命周期，再以独立 Spec 完成 Forward Renderer；
- pass 资源与依赖由 SceneRenderer/业务 Pass 明确管理；
- 先设计共享 TaskSystem，再增加每线程 context/pool 与 pass 间并行录制；
- compute context 和 dispatch；
- 在真实依赖形成后增加 RDG，由其逐步接管 barrier、transient 资源和调度；
- async compute 或其他高级能力继续后置。

## 18. 第一阶段验收条件

- 公共 RHI 头文件不包含后端原生类型或后端判断。
- Render pass、Material 和 scene 代码不直接访问全局 RHI 指针。
- GlobalShader test pass 无 validation error 完成 acquire、render pass、draw、submit 和 present。
- Resize、out-of-date 和 suboptimal 路径可恢复。
- Buffer/texture 创建、initial upload、transition、view 和 deferred deletion 形成闭环。
- 多 frame-in-flight 不提前 reset descriptor/command pool，不提前销毁资源。
- Shader、binding layout 和 pipeline 使用稳定完整 key，hash collision 不返回错误对象。
- BasePass 能构建 `MeshPassDrawList`/`MeshDrawCommand`，并按 Global、View、Pass、Material、Object 逻辑组组合完整 binding snapshot。
- 后端不支持功能返回可诊断的 `Unsupported`，不存在默认空操作成功。

## 19. 待定事项

以下事项在对应实现阶段定型，不阻塞当前公共边界：

- GlobalShader/MaterialShader 使用显式 registry 还是轻量注册宏；
- shader 编译工具链和 Vulkan/D3D bytecode 产物格式；
- transient uniform allocator 的公共 API 形态；
- Global、View、Pass、Material、Object 固定为五个逻辑 group；各 target/profile 的 physical set/register/root mapping 独立版本化；
- Material 的序列化格式与未来 Material Graph 生成接口；
- 第一阶段是否缓存静态 mesh draw packet；
- RDG 的具体资源声明与编译模型；在显式 Renderer 与真实资源生命周期完成验收后另立 Spec。
