## Context

动机见 [proposal.md](./proposal.md)。当前 `ForwardSceneRenderer` 的单个 `.cpp` 同时实现：

```text
init_views
→ compute_view_visibility
→ collect_mesh_batches
→ prepare_base_pass
→ execute_base_pass
```

其中私有 `PreparedBasePass` 同时包裹 `RHIRenderPassDesc` 和 draws，`PreparedViewBinding` 只在 BasePass 函数内部按 `ShaderMapProgram* + RHIBindingLayout*` 避免部分重复。这解决了局部重复 upload，但没有形成可供 ShadowPass 等后续 pass 使用的稳定职责边界。

本设计受以下既有 contract 约束：

- RHI render pass 只表达 attachment scope，不承担业务 pass 调度；后续 RDG 位于 renderscene。
- Global、View、Pass、Material、Object 是 logical Binding Group。Vulkan ES3.1 profile 最多四个 bound descriptor sets，由 backend 将 Global+View 聚合到 physical set 0；D3D11/D3D12 使用各 target mapping。
- 第一阶段一个 viewport frame 只录制一个 graphics command list；BasePass、Tonemap 和 ImGui 使用独立 RHI render-pass scope，但共同提交。
- pipeline、binding set 与 uniform upload 必须在 RHI render-pass scope 外物化。
- 当前工作区的 `forward_scene_renderer.cpp` 和相关测试含未提交的实验性 View binding 复用，实施时应将其意图纳入正式结构，不覆盖或保留第二套路径。

UE4.27 参考事实是：`FViewInfo` 保存单 View 的 render-side 派生状态与 View uniform resource；visibility 由 `FSceneRenderer::ComputeViewVisibility()` 配合 `SceneVisibility.cpp` 中短生命周期 work packet 完成；具体 mesh pass 使用 `FMeshPassProcessor`/`FMeshDrawCommand`；BasePass 由具体 renderer 方法和文件编排，并不存在所有业务 pass 共同继承的 `RenderPass` OO 基类。Toy3d 采用这些职责术语，但不复制 UE 的宏、对象系统、cached draw command registry、parallel relevance packets 或 RDG 复杂度。

## Goals / Non-Goals

**Goals:**

- 让 `ForwardSceneRenderer` 收敛为 frame/view/pass orchestration，而不是单 draw 资源物化器。
- 建立能被 BasePass 立即使用、也能被 ShadowPass 复用的数据层级和目录规则。
- 明确 `ViewInfo`、visibility、具体 pass 与 RHI 的所有权/生命周期边界。
- 将 View 参数成本从“每 MeshBatch 序列化并上传”收敛为“每 View 一次 uniform buffer；每个确有差异的 binding layout 一个轻量 adapter”。
- 保持 Vulkan、D3D11、D3D12 和 Vulkan ES3.1 profile 的公共实现可行性。

**Non-Goals:**

- 本 change 不实现 ShadowPass、DepthPass、VelocityPass、occlusion/HZB、LOD 或并行 visibility。
- 不引入 RDG、通用 Pass Scheduler、通用 `RenderPass` 基类、pass registry 或 pass 级并行录制。
- 不提取通用 `MeshPassProcessor` 基类；只有 ShadowPass 等实际落地并产生重复后再评估。
- 不新增跨帧 `ViewState`、persistent descriptor cache、bindless、dynamic offset 或 push/root constants 优化。
- 不修改公共 `RHIGraphicsBindings`、Vulkan physical set mapping 或 Shader target ABI。

## Decisions

### 1. 使用单数 `pass/` 目录和具体 pass 入口

新增文件组织为：

```text
engine/runtime/renderscene/
├── pass/
│   ├── base_pass.h
│   ├── base_pass.cpp
│   └── mesh_draw_command.h
└── view/
    ├── scene_visibility.h
    └── scene_visibility.cpp
```

`mesh_draw_command.cpp` 只在 `MeshDrawCommand`/`MeshPassDrawList` 出现非平凡共享实现时添加；纯 value contract 保持 header-only。后续 pass 直接增加 `shadow_pass.*`、`depth_pass.*`、`velocity_pass.*`。

BasePass 使用无状态业务入口：

```cpp
RHIStatus render_base_pass(
    RHIDevice& device,
    RHIShaderProgramCache& shader_program_cache,
    RHIGraphicsCommandContext& context,
    const BasePassInputs& inputs);
```

选择函数而不是 `BasePass` 对象，是因为当前没有需要跨调用保存的状态、独立生命周期或同步语义。选择具体入口而不是通用基类，是因为 BasePass、ShadowPass 和 fullscreen/postprocess pass 的 attachment、visibility、shader selection 与 draw policy 不同；现在抽象只会统一函数形状。

### 2. 用 draw list/command 取代 `PreparedBasePass`

`PreparedBasePass` 删除，不提供兼容 alias。目标数据形状为：

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

`MeshDrawCommand` 保存完整 logical binding snapshot，是因为 `RHIGraphicsCommandContext::bind_graphics_bindings()` 的公共语义是原子替换完整五组快照，且不同 Shader Program 可能需要不同 binding layout adapter。command 中重复保存 `RHIBindingSetRef` 只复制轻量强引用，不重复创建 uniform bytes/buffer；这也使执行循环无需回读 View、Material 或 pass sources。

`MeshPassDrawList` 不保存 `RHIRenderPassDesc`。BasePass 在局部先构造全部 draw lists，成功后根据 `BasePassInputs` 创建自己的 attachment descriptor并执行。这样：

- draw data 可供 ShadowPass 等 mesh pass 复用；
- attachment/load/store/clear 仍由具体 pass 决定；
- 多 View 通过多个 draw list 的 viewport/scissor 表达；
- 未来排序仅重排 command，不改变 attachment scope。

未采用“把 Global/View/Pass 单独只存一份在 draw list”的方案，因为当前 `RHIBindingSet` 需要与 program binding layout 兼容；强行单例化会隐含所有 material program 的完整 layout 相同。以后若 RHI 支持独立 group layout identity，可在不改变 logical ownership 的前提下压缩引用。

### 3. BasePass 内部保持 prepare-then-execute，但不公开 prepare wrapper

`render_base_pass(...)` 内部按以下连续阶段执行：

```text
validate BasePassInputs/attachments/device ownership
→ create RHIRenderPassDesc locally
→ for each ViewInfo / candidate MeshBatch
    resolve shader + effective state
    build vertex input + pipeline
    resolve View adapter from View buffer and compatible layout
    materialize Material/Object bindings
    append MeshDrawCommand
→ all draw lists complete
→ begin_render_pass
→ execute prepared commands
→ end_render_pass
```

这保留“所有创建与 upload 先于 `begin_render_pass()`”的硬边界，同时去掉外部可见的两段式 API 和 `PreparedBasePass` 生命周期。局部 helper 优先使用普通函数/lambda；若实现确需新增 cpp-private named type，必须先回到 mesh-pass spec 的 `Type Contracts` 登记，不能临时引入 `BasePassMeshProcessor` 空壳。

准备单个无效 MeshBatch 继续沿用现有策略：记录可定位诊断、跳过该 batch、允许其他合法 batch 进入 list。Attachment、device ownership 或 render-pass scope 等 pass 级失败会终止整个 pass。begin 后失败时保存首个业务错误并尽力 `end_render_pass()` 恢复 context 状态，外层随后 discard/abort，绝不提交部分结果。

### 4. `BasePassInputs` 只暴露 BasePass 声明的资源

建议定义：

```cpp
struct BasePassInputs
{
    const std::vector<ViewInfo>& views;
    RHITextureViewRef scene_color;
    RHITextureViewRef scene_depth;
};
```

`views` 是调用期间 non-owning reference；attachment refs 是按值持有的 RHI strong refs。BasePass 不接收完整 `SceneRenderTargets`，避免隐式访问未来 GBuffer、velocity 或 final output。BasePass 也不接收 viewport/frame/queue，因其不负责 submit/present/frame closure。

未直接传 `RHIRenderPassDesc`，是为了让具体 pass 拥有并验证自己的 load/store/clear contract。若未来同一 BasePass 支持明确不同的 load policy，应增加有业务语义的 input 字段，而不是让调用方任意注入完整 native-like pass descriptor。

### 5. `ViewInfo` 是单帧 camera view 状态，不是 pass 容器

`ViewInfo` 保留：

- owned `SceneView` value；
- view/projection/view-projection 与 inverse matrices；
- reversed-Z `ConvexVolume`；
- `ViewUniformShaderParameters` canonical CPU values；
- 当前 View 的 uniform buffer，以及需要时按 compatible binding layout 建立的 adapter cache；
- 当前帧 visible primitives 和 candidate MeshBatches。

`ViewInfo` 不保存：

- BasePass/ShadowPass attachment、pass binding、pipeline 或 draw list；
- command context、queue、frame slot 或 submit/completion 状态；
- Vulkan descriptor pool/physical packet 或其他 backend object；
- shadow light/cascade/atlas/depth bias；
- temporal history。

这与 UE4.27 的“View render-side derived state + View uniform resource”方向一致，但 Toy3d 不引入 UE 的大量 feature arrays、uniform buffer variants、view extension、parallel packet 或跨帧 ViewState。未来 temporal AA/occlusion history 出现后另立 `ViewState` contract。

### 6. View uniform 分为一次数据物化与按 layout 适配

View binding 准备拆成两层：

```text
每 View 一次：
validated ViewInfo values
→ ShaderMap member identity/type/offset/stride serialization
→ canonical ABI bytes
→ uniform buffer allocation + upload

每个实际需要的 compatible binding layout：
View uniform buffer
→ RHIBindingSet adapter
```

第一层在 visibility 后、首个消费 View group 的业务 pass 前完成并将资源存入本帧 `ViewInfo`。第二层可由 BasePass 在构建 draw commands 时按 layout 懒创建，并在同一 View 内复用。不能 raw-copy C++ struct representation。

这种拆分修正了当前 `materialize_view_uniform_shader_parameters(...)` 将 serialization、upload 与 layout-specific binding set 一次完成的问题。它也解释了“每个 draw command 仍持有 View binding ref”与“View 参数只上传一次”并不冲突。

Global 和 Pass 沿用相同原则但不在本 change 虚构参数源：Global 由 renderer 生命周期/帧策略拥有，Pass 由具体 pass 拥有；当前 Program 声明尚未实现的 Global/Pass 参数时仍诊断并跳过 batch。Material/Object 继续按各自既有 owner 和更新频率物化。

### 7. Visibility 使用 stateless 模块，不新增 `SceneVisibility` 类

导出明确操作：

```cpp
void compute_scene_visibility(
    const RenderScene& render_scene,
    std::vector<ViewInfo>& view_infos);
```

实现先清空每个 View 的 visible primitives/MeshBatches，再完成 primitive frustum culling 和 candidate MeshBatch gather。无效单个 primitive/proxy/render representation 记录诊断并跳过，不让 stateless 函数持有可失败的半成品状态；`init_views()` 已负责会阻断整帧的 View 输入校验。

未新增 `SceneVisibility` owner，是因为当前只有一次同步线性遍历，没有资源所有权、跨阶段 mutable state 或并行 join 生命周期。UE4.27 也主要由 renderer 的 visibility 方法、`SceneVisibility.cpp` 算法以及短生命周期 relevance packets协作，而不是一个长期 `FSceneVisibility` owner。未来出现并行 packets、HZB/occlusion 或多阶段 relevance 时，再根据真实生命周期登记 `SceneVisibilityContext` 或其他最终名称。

Camera 与 Shadow visibility 不共享结果。ShadowPass 后续根据 light/cascade/face volume 产生自己的 candidates/draw lists，因此 camera 外但 shadow volume 内的 caster 不会被错误删除。

### 8. `ForwardSceneRenderer` 只保留 orchestration

收敛后的调用关系为：

```text
ForwardSceneRenderer::render_scene_passes
    init_views()
    compute_scene_visibility(render_scene, view_infos)
    prepare_view_uniform_resources(...)
    transition SceneColor/SceneDepth
    render_base_pass(...)

Renderer outer frame orchestration
    render_scene_passes(...)
    transition SceneColor for sampling
    render_tonemap_pass(...)
    render_imgui_pass(...) when present
    final transitions / finish / end_frame
```

ShadowPass 接入后，可在 BasePass 前增加 shadow resource transition、shadow visibility 和 `render_shadow_pass(...)`，仍在同一 context 串行录制。它可为每个 cascade/face 生成 `MeshPassDrawList`，但使用自己的 pass parameters 与 attachment descriptor。等未来具备 pass 级独立 context 后，上述具体 pass 函数仍可作为并行 recording task 边界，无需重写为 OO pass hierarchy。

### 9. 跨后端映射保持在 RHI backend

| 语义 | Vulkan | D3D11 FL11_0 | D3D12 |
| --- | --- | --- | --- |
| 五个 logical groups | backend 聚合 Global+View 为 physical set 0，Pass/Material/Object 为 set 1/2/3 | 按 stage 与 CBV/SRV/sampler register class 展开 | 编译为 root/table mapping |
| View uniform 一次 upload | frame/context-local uniform allocation，descriptor adapter 可按 layout materialize | 一次 constant-buffer update，按 stage slots 引用 | 一次 upload/default resource 数据，按 root/table 引用 |
| MeshDrawCommand bindings | 录制时物化/复用 physical packets并由 command list 保活 | context state cache 可省略重复 slot bind | descriptor allocation/root binding由 command list 保活 |
| RHI render pass | attachment/layout/barrier mapping | OM target、clear/discard/resolve 组合 | render pass API 或等价命令组合 |

上层不查询 backend 名称或 physical set id。移动端仍只要求 Vulkan 1.1、SPIR-V 1.3 和最多四个 bound descriptor sets；本设计没有增加第五个 set，也不依赖 descriptor indexing、update-after-bind 或 dynamic uniform offset。

### 10. 测试边界

将现有依赖 private-member access 的 `ForwardSceneRenderer::compute_view_visibility` 测试迁移为直接测试 public/stateless visibility operation。新增或更新测试覆盖：

- 两个 View 独立 frustum、visible primitives 和 candidate MeshBatches；
- stale results 每次计算被清空；无效 bounds/缺失 render representation 可诊断跳过；
- `ViewInfo` 不持有具体 pass 或 backend 类型的 compile-time/结构检查；
- 同一 View 多个 MeshBatch 只发生一次 canonical View serialization/upload；相同 layout 复用 adapter，不同 layout 只新增 adapter、不新增 View buffer upload；
- BasePass 在 `begin_render_pass()` 前完成所有 create/upload，draw loop 不回读准备源；
- attachment/pass-level failure、单 batch skip 和 begin 后 command failure 的闭合行为；
- `PreparedBasePass`、旧 private prepare/execute/visibility/collect 入口完全删除；
- 原有 cube path、BasePass→Tonemap→可选 ImGui 单 graphics list 顺序保持通过。

## Risks / Trade-offs

- [Risk] 每个 `MeshDrawCommand` 保存完整 `RHIGraphicsBindings` 会重复若干 `shared_ptr` 引用并增加 frame-local CPU 内存 → 第一阶段优先保证 layout 正确性和直接执行；通过 sort/state cache 降低 RHI bind，只有 profiling 证明引用成本显著时再引入明确的 binding bucket。
- [Risk] `ViewInfo` 同时包含 CPU view values 和本帧 RHI resource，使纯 CPU 测试构造更复杂 → GPU resource 保持可空并只在显式 prepare 阶段发布；`init_views()` 仍可独立完成 CPU validation。
- [Risk] visibility 同时 cull 和 gather 可能随更多 proxy 类型增长 → 保持 gather 只产生 candidate MeshBatch，不下沉 pass eligibility；出现多个 producer 后再引入已登记的分派机制。
- [Risk] 当前 active `add-tonemap-imgui-output-passes` 与本 change 同时修改 `view-render-flow` 和总控帧顺序 → 实施前先完成或同步该 change，以其 Tonemap/ImGui contract 为基线合并，不能用当前 main spec 的旧 BasePass-only 文本覆盖。
- [Risk] active `rhi-design.md` 仍使用 `PreparedBasePass`/`MeshDrawPacket` 名称 → 同一实施批次更新对应章节和验收项，OpenSpec 与 Active 文档不得长期冲突。
- [Trade-off] 第一版 ShadowPass 可能仍需要局部重复 mesh processing 代码 → 这是有意延后抽象；只有至少两个具体 pass 的差异/重复可见后，才判断普通 helper、composition 或 `MeshPassProcessor` 基类何者合理。

## Migration Plan

1. 先确认 `add-tonemap-imgui-output-passes` 的已实施状态并以其最终帧顺序为合并基线；保留用户已有的未提交 View binding reuse 改动意图。
2. 新增 `pass/mesh_draw_command.h`、`pass/base_pass.*` 与 `view/scene_visibility.*`，登记 CMake sources；不保留旧/新双轨正式入口。
3. 将 visibility/collect 实现和测试迁到 stateless 模块，删除 `ForwardSceneRenderer` 的对应 private methods 与 private-member test access。
4. 将 View uniform serialization/upload 从 per-layout materializer 拆为 per-View buffer prepare 和 per-layout adapter，更新 `ViewInfo` 生命周期与复用测试。
5. 在 BasePass 模块中构造 `MeshPassDrawList`/`MeshDrawCommand` 并执行，迁移 attachment validation、pipeline/binding materialization 和 draw error closure。
6. 将 `ForwardSceneRenderer` 改为构造 `BasePassInputs` 和调用具体模块，删除 `PreparedBasePass`、旧 prepare/execute methods 与已被替代 helper。
7. 更新 `document/rhi-design.md` 的 pass/BasePass/MeshDraw 章节，并验证不存在 `PreparedBasePass`、`MeshDrawPacket` 或旧入口残留。
8. 完成定向单元测试、受影响 target 构建、全量 CTest 与 Vulkan Editor/Cube 冒烟；若失败，回滚整个模块切换而不是恢复长期双轨 API。

## Open Questions

- `mesh_draw_command.cpp` 是否需要存在取决于首批实现是否产生非平凡共享行为；若仅有 value types则保持 header-only，不改变 capability contract。
- ShadowPass 落地后是否需要抽取通用 `MeshPassProcessor`，必须基于 BasePass/ShadowPass 的实际重复重新评审；该问题不影响本 change 的数据 contract 和任务拆分。
