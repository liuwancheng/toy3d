## MODIFIED Requirements

### Requirement: init_views 创建完整 ViewInfo
`ForwardSceneRenderer::init_views()` MUST 在 RT 为 `SceneViewFamily` 中每个 `SceneView` 创建一个 `ViewInfo`，校验非空 view rect、有效输出尺寸、正的 near plane、投影模式以及全部必要矩阵值有限，并计算 view、projection、view-projection、inverse matrices 及剔除所需的派生矩阵。每个 `ViewInfo` MUST 重置本帧可见结果并构造自己的 `ConvexVolume`，不得复用上一帧的可见集合。`ViewUniformShaderParameters` MUST从已经验证的 canonical `ViewInfo` values 初始化；它不得反向成为 CPU 矩阵和视锥校验的前置依赖。

`ViewInfo` SHALL 是单帧、单 camera view 的 render-side 状态，可持有其 `SceneView` values、canonical matrices、frustum、View uniform 参数、每 View 的 uniform buffer/resource adapters、visible primitives 与 candidate MeshBatches。它 MUST NOT 持有 BasePass/ShadowPass attachments、pass-specific bindings/pipelines/draw lists、command context/queue/frame slot、Vulkan physical packets，或 shadow light/cascade/atlas 数据。跨帧 temporal history 后续若需要 MUST由独立且先登记的 View state contract 承担。

`init_views()` MUST NOT 拥有、查询或闭合 `RHIViewportContext`/`RHIFrameContext`。它只返回 CPU per-view 初始化的可诊断结果；是否已经 acquire frame 以及失败后调用 `abort_frame()` 的责任属于外层 Draw/frame orchestration。

#### Scenario: 多 View 初始化
- **WHEN** 一个 SceneViewFamily 含两个合法 SceneView
- **THEN** `init_views()` MUST 创建两个相互独立的 ViewInfo、canonical matrices、ConvexVolume、View parameters 和本帧空可见结果

#### Scenario: View 输入无效
- **WHEN** view rect 为空、输出尺寸无效、near plane 非正、矩阵包含非有限值或有效 frustum plane 退化
- **THEN** `init_views()` MUST 返回可诊断失败，后续 visibility 和业务 pass MUST NOT 录制；若外层 frame owner 已经 acquire frame，则外层 MUST 通过 `abort_frame()` 闭合

### Requirement: compute_view_visibility 线性剔除 PrimitiveSceneInfo
完成 `init_views()` 后，`ForwardSceneRenderer` MUST调用独立 visibility 模块的 `compute_scene_visibility(...)`，由其为每个 `ViewInfo` 线性剔除当前 `RenderScene` 的 `PrimitiveSceneInfo` 并收集 candidate MeshBatches。`ForwardSceneRenderer` MUST NOT再拥有 private visibility/collect implementation；具体 mesh pass MUST只消费 visibility 模块生成的当前帧 candidates。

第一阶段 culling、无效输入、接触平面、清空旧结果及非目标行为 MUST遵守 `game-render-framework/scene-visibility` capability。

#### Scenario: 一个可见和一个被剔除的 StaticMesh
- **WHEN** Scene 中两个 StaticMeshSceneProxy 只有一个通过当前 ViewInfo 的 frustum test
- **THEN** visibility 模块 MUST只为该 View 收集可见 Proxy 的 candidate MeshBatch，后续 BasePass不得重新遍历完整 RenderScene

#### Scenario: AABB 接触视锥平面
- **WHEN** Primitive world bounds 的最外点 signed distance 恰好为零且没有位于其他平面外侧
- **THEN** 独立 visibility 模块 MUST让对应 PrimitiveSceneInfo 保持可见

#### Scenario: 同一 Primitive 对两个 View 可见性不同
- **WHEN** Primitive world bounds 只与 SceneViewFamily 中一个 ViewInfo 的 ConvexVolume 相交
- **THEN** visibility 模块 MUST只将 Primitive 写入该 ViewInfo 的本帧可见结果

### Requirement: Forward Base Pass 消费可见 MeshBatch
visibility 完成后，独立 `pass/base_pass.*` 模块的 `render_base_pass(...)` MUST逐 View 消费 candidate `MeshBatch`，匹配 `LocalVertexFactory` 与 `ShaderVertexInput`，从 Material active candidate 获取 effective `ShaderGraphicsPassState`，解析 Global、View、Pass、Material、Object logical bindings，并将前向绘制录制到调用方提供的当前 graphics context。`ForwardSceneRenderer` MUST只构造 pass inputs、安排所需 transition 并调用该入口，不得物化单个 batch 的 shader、pipeline 或 binding。

BasePass MUST 使用 Shader effective state、LocalVertexFactory vertex layout 与调用方提供的 color/depth attachment format、sample count 合成完整 `RHIGraphicsPipelineDesc`。它 SHALL 自己 begin/end 对应 render pass，但 MUST NOT 创建/finish command list、submit、present、wait 或持有 viewport frame。Global/Pass group 未被 Program 声明时对应 binding set 保持 null；Program 声明了当前尚无 canonical parameter source 的 Global/Pass binding 时，当前 batch MUST 被诊断并跳过，不得伪造空 buffer 或无操作成功。

View/Object constant materialization MUST 根据 ShaderMap member identity、`ShaderValueType`、offset、size 与 matrix stride 逐字段写入 ABI byte buffer；不得 raw-copy `ViewUniformShaderParameters` 或 `PrimitiveUniformShaderParameters` 的 C++ object representation。一个 View 的 canonical View bytes 与 uniform buffer MUST在该 View 使用它们的业务 pass 前至多物化一次；不同 program layout 需要的轻量 binding adapter MAY按 layout 创建和复用，不能按 MeshBatch 重复上传相同 View 数据。Program 未使用某组时不创建对应资源；未知成员、类型不匹配、越界或暂不支持的资源类 binding MUST 诊断并跳过当前 batch。

第一阶段尚未登记 per-draw dynamic-state override source。BasePass MUST 在每个 draw 前显式设置 blend constants 为 white `(1, 1, 1, 1)`、stencil reference 为 `0`，使 `ConstantColor`/`OneMinusConstantColor` 与启用 Stencil 的合法 Shader state 在 Vulkan、D3D11 和 D3D12 具有确定且一致的默认值；不得依赖 backend 未初始化 state。

Vertex input 不兼容、`StaticMeshRenderData` 未通过整体可绘制 gate、其他必要 render representation 当前不可用、material/shader 无效或必要 binding 缺失时，BasePass MUST 跳过对应 batch并产生可诊断错误；不得创建残缺 pipeline、访问失效资源或无操作后报告成功。具体准备与录制边界 MUST遵守 `game-render-framework/mesh-pass-organization` capability。

#### Scenario: 一个可见和一个被剔除的 StaticMesh
- **WHEN** 当前 View 的 candidates 只包含通过 visibility 的 StaticMesh
- **THEN** `render_base_pass(...)` MUST只处理这些 candidates，不得再次处理被剔除 Proxy

#### Scenario: VertexFactory 与 Shader 输入不兼容
- **WHEN** LocalVertexFactory 缺少 ShaderVertexInput 的必要 attribute
- **THEN** 对应 MeshBatch MUST 被跳过并记录可诊断错误，其他合法 batch MAY 继续准备

#### Scenario: Program 声明暂不可提供的 Global 或 Pass binding
- **WHEN** ShaderMap Program 声明 Global 或 Pass group 的 active binding，但当前 Renderer/Pass 没有已登记的 canonical parameter source
- **THEN** 对应 MeshBatch MUST 被跳过并产生可诊断错误，不得绑定空 set 后继续 draw

#### Scenario: Base Pass 录制边界
- **WHEN** 外层已提供 graphics context、合法 BasePass inputs 且 draw resources 已在 render-pass scope 前物化
- **THEN** BasePass MUST begin render pass、录制全部合法 draw commands并 end render pass，但 MUST NOT finish、submit、present 或 wait

#### Scenario: 同 View 的 View uniform 复用
- **WHEN** 同一 View 的多个 MeshBatch 使用相同 canonical View values
- **THEN** BasePass MUST复用该 View 的 uniform buffer，并仅在 binding layout 确实不兼容时建立额外 adapter

### Requirement: 一帧只录制一个 graphics list
第一阶段每个 viewport Draw SHALL `begin_frame()`、创建一个 graphics context、录制 pending uploads、执行 `init_views()`、调用独立 `compute_scene_visibility(...)`、准备 View uniform resources、录制全部正式 scene graphics pass、以独立 Tonemap pass 把 HDR SceneColor 输出到最终目标、录制可选的独立 ImGui pass、finish 一个 immutable list 并 `end_frame()`。BasePass、未来 ShadowPass、Tonemap 和 ImGui MUST使用该 context，不得创建隐藏 command list、独立 submit 或等待；每个具体 pass MUST拥有独立 RHI render-pass attachment scope。

#### Scenario: Cube project 正常帧
- **WHEN** Cube project 投递包含可见 StaticMesh 和可选 UI payload 的一次 Draw
- **THEN** pending uploads、View初始化、scene visibility、View uniform准备、Forward BasePass、Tonemap和可选ImGui MUST按显式顺序串行录入同一context，并由一次 `end_frame()` 提交

#### Scenario: ImGui payload 为空
- **WHEN** 当前 frame 没有 UI draw command
- **THEN** 同一 graphics list MUST仍完成 BasePass、Tonemap 与 Present transition，且不得创建第二个空 UI list 或空 render pass

## Minimal Implementation Example

以下示例是 non-normative；本 delta 不新增 `ViewInfo` 等具名类型，只收敛既有类型职责。

```cpp
if (!init_views())
{
    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "invalid view inputs");
}

compute_scene_visibility(render_scene, view_infos());
RHI_TRY(prepare_view_uniform_resources(device, context, view_infos()));

BasePassInputs base_pass_inputs{view_infos(), scene_color_view, scene_depth_view};
RHI_TRY(render_base_pass(device, shader_program_cache, context, base_pass_inputs));
// 外层随后在同一 context 录制 Tonemap/ImGui；任何失败都由 frame owner discard/abort。
```
