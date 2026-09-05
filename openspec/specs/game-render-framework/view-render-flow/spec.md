# game-render-framework/view-render-flow Specification

## Purpose
定义 Game Thread 创建一次性 View 输入和 SceneRenderer、Rendering Thread 初始化 View、执行 reversed-Z 视锥剔除并由前向 Base Pass 录制一帧的流程，避免为跨线程数据引入 Snapshot 命名或长期帧包。

## Requirements

### Requirement: GT 构造一次性 View 输入
GT SHALL 从 Camera、viewport、Window 与 Game 状态复制构造 `SceneView` 与 `SceneViewFamily`；`SceneView` MUST 独立保存相机位置/方向、view rect、输出尺寸和投影输入，不得保存 CameraComponent、Window、World 或其他 GT 可变对象的引用。构造期间 MUST NOT 读取 RT 可变 RenderScene。

#### Scenario: Camera 在投递后继续变化
- **WHEN** GT 已投递 frame N 的 SceneRenderer 后 CameraComponent 再次更新
- **THEN** frame N MUST 使用已拥有的 view values，不得回读 CameraComponent

### Requirement: SceneRenderer ownership 转移
`ForwardSceneRenderer` MUST 一次性拥有 `SceneViewFamily`，并以 `SceneRenderer` ownership 整体 move 进 Draw command；GT 投递后不再访问。logical RT MUST 使用 FIFO 前序 Proxy/Resource 更新后的 RenderScene 执行并析构该 SceneRenderer。

#### Scenario: Draw command 被 terminal skip
- **WHEN** terminal 发生在 Draw 执行前
- **THEN** SceneRenderer MUST 不执行，并在 logical RT command disposal 路径析构

### Requirement: init_views 创建完整 ViewInfo
`ForwardSceneRenderer::init_views()` MUST 在 RT 为 `SceneViewFamily` 中每个 `SceneView` 创建一个 `ViewInfo`，校验非空 view rect、有效输出尺寸、正的 near plane、投影模式以及全部必要矩阵值有限，并计算 view、projection、view-projection 及剔除所需的派生矩阵。每个 `ViewInfo` MUST 重置本帧可见结果并构造自己的 `ConvexVolume`，不得复用上一帧的可见集合。需要 View logical Binding Group 的业务 pass 录制前，`ViewUniformShaderParameters` MUST 从已经验证的 canonical ViewInfo values 初始化；它不得反向成为 CPU 矩阵和视锥校验的前置依赖。

`init_views()` MUST NOT 拥有、查询或闭合 `RHIViewportContext`/`RHIFrameContext`。它只返回 CPU per-view 初始化的可诊断结果；是否已经 acquire frame 以及失败后调用 `abort_frame()` 的责任属于外层 Draw/frame orchestration。

#### Scenario: 多 View 初始化
- **WHEN** 一个 SceneViewFamily 含两个合法 SceneView
- **THEN** `init_views()` MUST 创建两个相互独立的 ViewInfo、ConvexVolume 和本帧可见结果

#### Scenario: View 输入无效
- **WHEN** view rect 为空、输出尺寸无效、near plane 非正、矩阵包含非有限值或有效 frustum plane 退化
- **THEN** `init_views()` MUST 返回可诊断失败，后续 visibility 和业务 pass MUST NOT 录制；若外层 frame owner 已经 acquire frame，则外层 MUST 通过 `abort_frame()` 闭合

### Requirement: reversed-Z ConvexVolume 使用固定提取约定
View frustum MUST 遵守引擎固定的 left-handed、column-vector、column-major storage、clip depth 0..1 reversed-Z contract。对 view-projection matrix 的行向量 `r0`、`r1`、`r2`、`r3`，`ConvexVolume` MUST 按以下公式提取平面：

```text
clip contract:
-w <= x <= w
-w <= y <= w
 0 <= z <= w

Left   = r3 + r0
Right  = r3 - r0
Bottom = r3 + r1
Top    = r3 - r1
Far    = r2
Near   = r3 - r2
```

用于 signed-distance 或 bounds 测试前，每个启用的 `Plane` MUST 归一化并以正半空间表示视锥内部。finite perspective MUST 启用六个平面；infinite-far projection MUST 禁用 far plane 而保留其余五个平面。不得由 Shader 或 Vulkan 上层代码追加 Y 翻转或替换该公共公式。

#### Scenario: Infinite-far View
- **WHEN** SceneView 使用 infinite-far projection
- **THEN** ConvexVolume MUST 只使用 Left、Right、Bottom、Top、Near 五个有效平面，远距离对象不得被伪造的 far plane 剔除

### Requirement: compute_view_visibility 线性剔除 PrimitiveSceneInfo
`ForwardSceneRenderer::compute_view_visibility()` 第一阶段 MUST 对每个 `ViewInfo` 线性遍历 RenderScene 中仍注册的 `PrimitiveSceneInfo`，排除 visibility 关闭、world bounds 无效或已移除的 Proxy，再用 world-space AABB 与该 ViewInfo 的 `ConvexVolume` 相交测试生成仅当前帧有效的可见结果。AABB 接触任一 plane MUST 视为可见。

第一阶段 MUST NOT 引入 octree、occlusion culling、distance culling、LOD、跨帧 visibility cache 或为剔除新增 Handle/Token 类型。

#### Scenario: AABB 接触视锥平面
- **WHEN** Primitive world bounds 的最外点 signed distance 恰好为零且没有位于其他平面外侧
- **THEN** 对应 PrimitiveSceneInfo MUST 保持可见

#### Scenario: 同一 Primitive 对两个 View 可见性不同
- **WHEN** Primitive world bounds 只与 SceneViewFamily 中一个 ViewInfo 的 ConvexVolume 相交
- **THEN** Primitive MUST 只进入该 ViewInfo 的本帧可见结果

### Requirement: Forward Base Pass 消费可见 MeshBatch
visibility 完成后，仍可见的 `StaticMeshSceneProxy` SHALL 为当前帧贡献 `MeshBatch`。`ForwardSceneRenderer::render_base_pass()` MUST 逐 View 消费这些 batch，匹配 `LocalVertexFactory` 与 `ShaderVertexInput`，从 Material active candidate 获取 effective `ShaderGraphicsPassState`，解析 Global、View、Pass、Material、Object bindings，并将前向绘制录制到调用方提供的当前 graphics context。

Base Pass MUST 使用 Shader effective state、LocalVertexFactory vertex layout 与调用方提供的 color/depth attachment format、sample count 合成完整 `RHIGraphicsPipelineDesc`。它 SHALL 自己 begin/end 对应 render pass，但 MUST NOT 创建/finish command list、submit、present、wait 或持有 viewport frame。Global/Pass group 未被 Program 声明时对应 binding set 保持 null；Program 声明了当前尚无 canonical parameter source 的 Global/Pass binding 时，当前 batch MUST 被诊断并跳过，不得伪造空 buffer 或无操作成功。

第一阶段尚未登记 per-draw dynamic-state override source。Base Pass MUST 在每个 draw 前显式设置 blend constants 为 white `(1, 1, 1, 1)`、stencil reference 为 `0`，使 `ConstantColor`/`OneMinusConstantColor` 与启用 Stencil 的合法 Shader state 在 Vulkan、D3D11 和 D3D12 具有确定且一致的默认值；不得依赖 backend 未初始化 state。未来开放动态 override 时 MUST 先登记其 ownership、线程和生命周期 contract。

View/Object constant materialization MUST 根据 ShaderMap member identity、`ShaderValueType`、offset、size 与 matrix stride 逐字段写入 ABI byte buffer；不得 raw-copy `ViewUniformShaderParameters` 或 `PrimitiveUniformShaderParameters` 的 C++ object representation。Program 未使用某组时不创建对应资源；未知成员、类型不匹配、越界或暂不支持的资源类 binding MUST 诊断并跳过当前 batch。

Vertex input 不兼容、`StaticMeshRenderData` 未通过整体可绘制 gate、其他必要 render representation 当前不可用、material/shader 无效或必要 binding 缺失时，Renderer MUST 跳过对应 batch 并产生可诊断错误；不得创建残缺 pipeline、访问失效资源或无操作后报告成功。

#### Scenario: 一个可见和一个被剔除的 StaticMesh
- **WHEN** Scene 中两个 StaticMeshSceneProxy 只有一个通过当前 ViewInfo 的视锥测试
- **THEN** `render_base_pass()` MUST 只处理可见 Proxy 贡献的 MeshBatch

#### Scenario: VertexFactory 与 Shader 输入不兼容
- **WHEN** LocalVertexFactory 缺少 ShaderVertexInput 的必要 attribute
- **THEN** 对应 MeshBatch MUST 被跳过并记录可诊断错误，其他合法 batch MAY 继续录制

#### Scenario: Program 声明暂不可提供的 Global 或 Pass binding
- **WHEN** ShaderMap Program 声明 Global 或 Pass group 的 active binding，但当前 Base Pass 没有已登记的 canonical parameter source
- **THEN** 对应 MeshBatch MUST 被跳过并产生可诊断错误，不得绑定空 set 后继续 draw

#### Scenario: Base Pass 录制边界
- **WHEN** 外层已提供 graphics context 与合法 color/depth attachments
- **THEN** Base Pass MUST begin render pass、录制全部合法 batch 并 end render pass，但 MUST NOT finish、submit、present 或 wait

### Requirement: 一帧只录制一个 graphics list
第一阶段每个 viewport Draw SHALL `begin_frame()`、创建一个 graphics context、录制 pending uploads、执行 `init_views()` 与 `compute_view_visibility()`、录制全部正式 graphics pass、finish 一个 immutable list 并 `end_frame()`。Base Pass MUST 使用该 context，不得创建隐藏 command list、独立 submit 或等待 GPU。

#### Scenario: Cube project 正常帧
- **WHEN** Cube project 投递包含可见 StaticMesh 的一次 Draw
- **THEN** pending uploads、View 初始化、visibility 和 Forward Base Pass MUST 按显式顺序串行录入同一 context，并由一次 `end_frame()` 提交

### Requirement: Cube project 验收正式 Game 到 Render 纵向链路
集中验收 SHALL 由 `Toy3dCubeTest` project executable 创建一个父 SceneComponent 与三个 attached child StaticMesh cubes，并通过 opt-in Actor Tick 更新 local rotation。它 MUST 使用正式 World/Actor/Component lifecycle、SceneInterface/RenderCommand transport、SceneViewFamily、visibility、MeshBatch、Material binding 与 Forward Base Pass；不得直接获取 RenderScene、RHI 或 backend，不得复制 Engine composition root，也不得以测试 pass 替代正式业务 pass。

Cube project 的 Phong Material SHALL 使用 POSITION0/NORMAL0、View canonical camera values、Object `toy_object_to_world` 和 Material directional-light/ambient/specular parameters。第一阶段场景 MUST 使用 uniform scale，使现有 Object binding 足以变换方向；非 uniform scale 所需的 inverse-transpose normal contract 不得由测试代码私自补充。

#### Scenario: 三个 attached child cubes 连续旋转
- **WHEN** GT tick 更新三个 child cubes 的 local rotation 并投递包含它们的 SceneViewFamily
- **THEN** attachment transform MUST 传播到 Primitive render state，logical RT MUST 按 FIFO 消费更新，visibility 通过的 cubes MUST 形成 MeshBatch 并由 Forward Base Pass 录制 indexed draw

### Requirement: Recoverable viewport 状态
NotReady、OutOfDate 和 Suboptimal MUST 由 SceneRenderer/Renderer frame policy 处理，不得让 GT 直接操作 swapchain 或 backend token。

#### Scenario: 最小化窗口
- **WHEN** begin frame 返回 NotReady
- **THEN** 本帧 MUST 不录制 Draw，pending resources 保持可在后续有效 frame 提交
