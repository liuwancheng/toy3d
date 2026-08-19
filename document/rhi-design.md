# Toy3d RHI 设计

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
- 简化的 BasePass、MeshPassProcessor 和 MeshDrawPacket。

第一阶段不实现 async compute、bindless、ray tracing、VRS、多 GPU、完整 Render Graph、RHI thread 和 draw batch 内并行。公共描述符和 binding layout 需要预留 compute 与 storage resource，但未实现功能必须返回 `Unsupported`，不得空操作成功。

RHI 图形闭环后的上层路线以 `rendering-engine-foundation-design.md` 为准：先使用显式、长期可保留的 SceneRenderer 与业务 Pass 完成 World/RenderScene、Game/Render Thread、Forward Renderer、PostProcess 与 ImGui，再基于真实资源依赖后置引入 RDG。该显式阶段不建立通用临时 Pass Scheduler。

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
    - MeshPassProcessor / MeshDrawPacket
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

`RHIDevice` 负责初始化、能力查询和资源创建，不负责 draw、render pass 或 submit。

```cpp
class RHIDevice
{
public:
    virtual ~RHIDevice() = default;

    virtual const RHICapabilities& capabilities() const = 0;
    virtual const RHILimits& limits() const = 0;

    virtual RHIResult<RHIBufferRef> create_buffer(
        const RHIBufferDesc& desc,
        const RHIInitialData* initial_data) = 0;

    virtual RHIResult<RHITextureRef> create_texture(
        const RHITextureDesc& desc,
        const RHIInitialData* initial_data) = 0;

    virtual RHIResult<RHITextureViewRef> create_texture_view(
        const RHITextureViewDesc& desc) = 0;

    virtual RHIResult<RHIShaderRef> create_shader(
        const RHIShaderDesc& desc) = 0;

    virtual RHIResult<RHIBindingLayoutRef> create_binding_layout(
        const RHIBindingLayoutDesc& desc) = 0;

    virtual RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline(
        const RHIGraphicsPipelineDesc& desc) = 0;
};
```

资源不得反向访问全局 device。公共头文件不得定义可变 `g_rhi` 指针；engine 初始化层显式拥有 device，并向 renderer 注入所需引用。

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
    virtual RHIStatus end_frame(
        std::unique_ptr<RHIFrameContext> frame,
        const std::vector<RHICommandListRef>& command_lists) = 0;
    virtual RHIStatus request_resize(uint32 width, uint32 height) = 0;
};
```

Swapchain 是各后端 `RHIViewportContext` 的内部 presentation 组件，不建立公共 `RHISwapchain` 或 `RHIDevice::create_swapchain()` 平行路径。`begin_frame()` 返回当前 presentation texture/view，`end_frame()` 统一完成 submit 和 present；image index、frame slot、acquire/present 同步对象及原生 swapchain 均不得泄漏到 renderscene。Out-of-date、suboptimal、surface lost、device lost 和延迟 resize 由 viewport 内部处理并通过可诊断结果反馈。

viewport status 中 `NotReady`、`OutOfDate` 与 `Suboptimal` 分别表达暂时无可用 extent、需要重建、
以及本帧完成但后续应重建，caller 可将它们作为 recoverable frame outcome。不可恢复的 submit、
同步或 surface failure 保持 `DeviceLost`、`BackendFailure` 等原始诊断，不得降格为 `NotReady`
而被渲染循环永久静默忽略。

## 5. 资源与 view

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
    RHIBindingGroup group;
    uint32 target_binding;
    RHIResourceBindingType type;
    RHIShaderStage stage;
    uint32 array_count;
};
```

Binding resource type 至少包括 uniform buffer、sampled texture、storage texture、sampler、storage buffer。Compute 和 storage binding 可从第一版进入 descriptor，但实际调用受 capability 控制。

每个 entry 只描述一个 stage。同一逻辑参数被多个 stage 使用时生成多个 entry，因此 D3D11/D3D12 可以为不同 stage 保存不同 target binding；Vulkan backend 可在编译 native layout 时合并具有相同 physical set/binding 的 stage visibility。`RHIResourceBindingType` 决定 target register/descriptor class，禁止用一个 `target_binding + stages bitmask` 丢失 per-stage mapping。

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

- Vulkan backend 将多个 logical group 按 profile 打包到 physical descriptor sets；logical group 与 descriptor set 不一一对应。`VulkanPortable v1` 固定 set 0=Global+View、set 1=Pass、set 2=Material、set 3=Object；
- D3D12 可编译为 descriptor table、root CBV 或 root constants；
- D3D11 展开为各 shader stage 的 CBV/SRV/UAV/sampler slot。

`target_binding` 是当前 Shader target record、当前 stage 已编译的 RHI 位置，不是跨 target 的资产级 slot。公共枚举数值不等于 Vulkan set index 或 D3D12 root parameter index。映射只存在于 target-specific binding layout 编译结果中。

`RHIGraphicsBindings` 的五个引用是逻辑数据包；Vulkan backend 在 draw/dispatch 的命令录制阶段、实际 bind 前，根据当前 Global/View pair 获取或构建组合 physical set 0，其他逻辑组分别 materialize 为 set 1..3。command list 保活这些 native set 和其引用资源直到 queue completion；不能推迟到 queue submit 时再改写已经录制的绑定。Pass 不得修改 Material binding，Material 也不得持有 SceneColor、SceneDepth 等 pass resource。Global/View/Pass binding 通常在 pass 开始时绑定，Material/Object binding 按 draw packet 更新。

## 11. Material 系统预留

Material 系统正式采用：

```text
Material → MaterialInstance → MaterialRenderProxy
```

不引入 `MaterialTemplate` 或 `MaterialInterface`。完整第一版范围见
`rendering-engine-foundation-design.md`。

### 11.1 `Material`

`Material` 是不可变资产定义，保存稳定参数 schema、静态渲染属性与 ShaderMap
reference。第一版静态属性包括 Phong shading model、`Opaque | Translucent` blend
mode 与 `two_sided`。静态属性参与 shader/PSO 选择，不作为普通 dynamic parameter
上传。

Game 侧共享引用为 `shared_ptr<const Material>`。Material 不保存 RHI object、Vulkan
descriptor set 或 D3D12 descriptor handle。

### 11.2 `MaterialInstance`

`MaterialInstance` 保存 `MaterialRef` 与由稳定 `ShaderParameterId` 标识的 typed
parameter override。参数类型必须匹配 Material schema；动态参数更新只增加 revision，
不触发 shader 编译。`StaticMeshComponent` 始终引用 MaterialInstance，不在 Component
内复制一套材质字段。

### 11.3 `MaterialRenderProxy`

`MaterialRenderProxy` 由 Render Thread 创建和拥有，保存已 resolve 的不可变参数快照、
texture/sampler strong references、ShaderMap program 与 Material binding。资源更新使用稳定
Render Resource ID 和单调 revision；正在录制或被 GPU 使用的旧版本由 RenderScene、
Prepared Frame 与 RHI completion 生命周期继续保活。

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

Render pass 分为资源声明和命令执行：

```cpp
class RenderPass
{
public:
    virtual ~RenderPass() = default;
    virtual void setup(RenderPassBuilder& builder) = 0;
    virtual RHIResult execute(RenderPassContext& context) = 0;
};
```

`setup()` 声明资源 read/write、attachment、load/store/clear 和预期 access。Scheduler 计算 pass 顺序和 transition。`execute()` 只录制当前 pass 的命令，不直接 submit，不访问 swapchain，不等待 device idle。

第一阶段 scheduler 串行运行即可，但每个 pass 应生成独立、结束后不可修改的 command list。后续可在 pass 之间并行录制，并按依赖拓扑提交。

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

BasePass 参考 UE4.27 的核心流程，但使用精简对象：

```text
Visible primitives
        |
        v
MeshBatch
        |
        v
BasePassMeshProcessor
    - pass eligibility
    - material fallback
    - shader variant
    - render state
    - sort key
        |
        v
MeshDrawPacket
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

`BasePassMeshProcessor` 属于 RenderCore/RenderScene，不属于 RHI：

```cpp
class BasePassMeshProcessor
{
public:
    void add_mesh_batch(
        const MeshBatch& mesh_batch,
        const PrimitiveSceneProxy& primitive);

private:
    bool process(
        const MeshBatch& mesh_batch,
        const MaterialRenderProxy& material);
};
```

BasePass 使用 MaterialShaderMap，根据 Material shader identity、静态属性、static switches、VertexFactory、pass type、feature level 和 target 获取 shader variant，再构建 draw packet。

### 13.4 `MeshDrawPacket`

```cpp
struct MeshDrawPacket
{
    RHIGraphicsPipelineRef pipeline;

    RHIBindingSetRef material_bindings;
    RHIBindingSetRef object_bindings;

    RHIVertexBufferBindings vertex_buffers;
    RHIIndexBufferRef index_buffer;
    RHIDrawIndexedArgs draw_args;

    uint64 sort_key = 0;
};
```

Global/View/Pass bindings 在 pass 开始时准备，Material/Object bindings 按 packet 更新；
每次 draw 提交一个完整 logical binding 快照：

```cpp
for (const MeshDrawPacket& packet : draw_packets)
{
    commands.set_graphics_pipeline(packet.pipeline);
    RHIGraphicsBindings bindings;
    bindings.global = global_bindings;
    bindings.view = view_bindings;
    bindings.pass = pass_bindings;
    bindings.material = packet.material_bindings;
    bindings.object = packet.object_bindings;
    commands.bind_graphics_bindings(bindings);
    commands.draw_indexed(packet.draw_args);
}
```

提交器可缓存当前 pipeline、vertex stream 和 binding set，减少重复 RHI 命令。缓存属于 command recording/RenderCore，不改变 draw packet 的语义。

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

Profile 是版本化的离线编译和验证基线，不等于 backend。平台配置选择默认 profile；runtime 仍以实际 `RHICapabilities`、`RHILimits` 和 format support 复核 ShaderPackage 的 requirements。默认 `VulkanPortable v1` 固定为 Vulkan 1.1、SPIR-V 1.3、最多四个 bound descriptor sets，且不默认依赖可选 device feature。

高于 portable 基线的功能必须由独立 profile 或 `Requires <Capability>` 显式声明，禁止根据当前桌面 GPU 自动提高 Cook 输出要求。Cook 和 runtime 都验证 sampler、sampled image、uniform/storage buffer、storage image 的 per-stage 与 pipeline-layout limits；错误需报告 group、stage、resource class、required 和 supported。ShaderPackage/ShaderCodeLibrary 保存 required capabilities/limits，不兼容时返回可诊断的 `UnsupportedCapability`。

### 15.2 统一图形约定

RHI pipeline 与 viewport 映射遵守 Shader 系统的统一约定：left-handed、+Z forward、column-vector、column-major、clip depth 0..1、reversed-Z（clear 0.0、默认 `GreaterEqual`）和公共 CounterClockwise front face。Vulkan 1.1 backend 使用 negative viewport height 处理 Y 时，必须同步修正 native front-face mapping；上层和 Shader 不做 backend-specific Y flip。

所有初始化、创建、map/update、acquire、submit、present 和 resize 操作返回可检查结果。Assert 只用于内部不变量，不能替代 Release 错误路径。

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
| submit completion | fence/timeline | query/fence strategy | fence value |
| async compute | capability gated | 通常不作为基线 | capability gated |

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
- 建立 `MeshBatch`、`BasePassMeshProcessor` 和 `MeshDrawPacket`；
- 完成 SceneColor/SceneDepth 的 BasePass 绘制闭环。

### 阶段 6：跨后端审计

- 对每个公共 descriptor 和命令写出 D3D11/D3D12 映射；
- 检查是否存在 Vulkan descriptor set、layout 或 stage mask 泄漏；
- 对 D3D11 不支持能力提供 capability/`Unsupported` 路径；
- 使用后端独立测试覆盖创建、binding、transition、draw 和 resize。

### 阶段 7：后续演进

- 按 `rendering-engine-foundation-design.md` 完成显式 Forward Renderer；
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
- BasePass 能通过 `BasePassMeshProcessor` 构建 `MeshDrawPacket`，并正确组合 Pass、Material 和 Object 参数。
- 后端不支持功能返回可诊断的 `Unsupported`，不存在默认空操作成功。

## 19. 待定事项

以下事项在对应实现阶段定型，不阻塞当前公共边界：

- GlobalShader/MaterialShader 使用显式 registry 还是轻量注册宏；
- shader 编译工具链和 Vulkan/D3D bytecode 产物格式；
- D3D11 queue completion value 的 fence/query 实现策略；
- transient uniform allocator 的公共 API 形态；
- Global、View、Pass、Material、Object 固定为五个逻辑 group；各 target/profile 的 physical set/register/root mapping 独立版本化；
- Material 的序列化格式与未来 Material Graph 生成接口；
- 第一阶段是否缓存静态 mesh draw packet；
- RDG 的具体资源声明与编译模型；引入条件和前置路线见 `rendering-engine-foundation-design.md`。
