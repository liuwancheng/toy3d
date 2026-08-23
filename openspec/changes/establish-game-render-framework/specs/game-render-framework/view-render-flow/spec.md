## Purpose

定义 Game Thread 创建一次性 View 输入和 SceneRenderer、Rendering Thread 初始化 View、执行 reversed-Z 视锥剔除并由前向 Base Pass 录制一帧的流程，避免为跨线程数据引入 Snapshot 命名或长期帧包。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `SceneView` | 新增 struct/class | GT 创建的一次性相机与 viewport value；move 到 RT 后只读；不引用 CameraComponent |
| `SceneViewFamily` | 新增 struct/class | 聚合一次 Draw 的 SceneInterface、views、output 与 show/config values；由 SceneRenderer 一次性拥有，不形成长期帧包 |
| `ViewInfo` | 新增 class | RT-only per-view 状态；由 SceneRenderer 从 SceneView 创建，保存派生矩阵、ConvexVolume 和仅当前帧有效的可见结果 |
| `SceneRenderer` | 新增 polymorphic class | GT 基于 SceneViewFamily 创建，Draw command 独占并移入 RT；RT 读取 RenderScene、录制 pass、执行后析构 |
| `ForwardSceneRenderer` | 新增 final class | SceneRenderer 的前向实现；依次执行 `init_views()`、`compute_view_visibility()` 与 `render_base_pass()` |
| `Plane` | 新增共享 math value type | 表达归一化平面及 signed-distance 测试；正半空间是 ConvexVolume 内部，不依赖 Renderer 或 RHI |
| `ConvexVolume` | 新增共享 math value type | 保存有效 Plane 集合并执行 point/bounds 相交测试；支持 finite 与 infinite-far frustum |

不得新增任何以 `Snapshot` 命名的类型。简单测试 pass SHALL 留在测试目标内部，不新增公共 test-pass 类型或运行时注册机制。

## ADDED Requirements

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
`ForwardSceneRenderer::init_views()` MUST 在 RT 为 `SceneViewFamily` 中每个 `SceneView` 创建一个 `ViewInfo`，校验非空 view rect、有效输出尺寸、正的 near plane、投影模式以及全部必要矩阵值有限，并计算 view、projection、view-projection 及剔除所需的派生矩阵。每个 `ViewInfo` MUST 重置本帧可见结果并构造自己的 `ConvexVolume`，不得复用上一帧的可见集合。

#### Scenario: 多 View 初始化
- **WHEN** 一个 SceneViewFamily 含两个合法 SceneView
- **THEN** `init_views()` MUST 创建两个相互独立的 ViewInfo、ConvexVolume 和本帧可见结果

#### Scenario: View 输入无效
- **WHEN** view rect 为空、输出尺寸无效、near plane 非正、矩阵包含非有限值或有效 frustum plane 退化
- **THEN** `init_views()` MUST 返回可诊断失败，后续 visibility、测试 pass 和业务 pass MUST NOT 录制，已 acquire frame MUST 通过 `abort_frame()` 闭合

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
visibility 完成后，仍可见的 `StaticMeshSceneProxy` SHALL 为当前帧贡献 `MeshBatch`。`ForwardSceneRenderer::render_base_pass()` MUST 逐 View 消费这些 batch，匹配 `LocalVertexFactory` 与 `ShaderVertexInput`，解析 View、Material、Object bindings，并将前向绘制录制到当前 graphics context。

Vertex input 不兼容、`StaticMeshRenderData` 未通过整体可绘制 gate、其他必要 render representation 当前不可用、material/shader 无效或必要 binding 缺失时，Renderer MUST 跳过对应 batch 并产生可诊断错误；不得创建残缺 pipeline、访问失效资源或无操作后报告成功。

#### Scenario: 一个可见和一个被剔除的 StaticMesh
- **WHEN** Scene 中两个 StaticMeshSceneProxy 只有一个通过当前 ViewInfo 的视锥测试
- **THEN** `render_base_pass()` MUST 只处理可见 Proxy 贡献的 MeshBatch

#### Scenario: VertexFactory 与 Shader 输入不兼容
- **WHEN** LocalVertexFactory 缺少 ShaderVertexInput 的必要 attribute
- **THEN** 对应 MeshBatch MUST 被跳过并记录可诊断错误，其他合法 batch MAY 继续录制

### Requirement: 一帧只录制一个 graphics list
第一阶段每个 viewport Draw SHALL `begin_frame()`、创建一个 graphics context、录制 pending uploads、执行 `init_views()` 与 `compute_view_visibility()`、录制全部 graphics pass、finish 一个 immutable list 并 `end_frame()`。运行时 Base Pass 与测试构建中的简单测试 pass MUST 复用该 context 和显式顺序，不得各自提交隐藏 command list。

#### Scenario: 多个业务 pass
- **WHEN** 测试构建在 Forward Base Pass 前录制简单测试 pass
- **THEN** pending uploads、View 初始化、visibility、测试 pass、Base Pass MUST 按显式顺序串行录入同一 context，结果不得依赖并行录制

### Requirement: 测试目标提供简单 render-side pass
Renderer 的集中测试目标 SHALL 提供一个仅存在于测试源码中的简单 pass 实现。该 pass MUST 消费同一帧 `ViewInfo`/visibility 输入，并向测试 render target 录制确定性的 clear 或简单 draw，以证明 Game 输入已到达 RT pass 录制阶段；它 MUST NOT 新增公共运行时类型、全局 pass registry 或第二套 Scene/View 数据模型。

#### Scenario: Game 到 Render 的最小纵向测试
- **WHEN** 测试在 GT 构造包含一个可见 Primitive 的 SceneViewFamily 并投递 Draw
- **THEN** RT MUST 完成 `init_views()` 和 visibility，简单测试 pass MUST 观察到对应可见输入并在同一 graphics context 录制确定性工作

### Requirement: Recoverable viewport 状态
NotReady、OutOfDate 和 Suboptimal MUST 由 SceneRenderer/Renderer frame policy 处理，不得让 GT 直接操作 swapchain 或 backend token。

#### Scenario: 最小化窗口
- **WHEN** begin frame 返回 NotReady
- **THEN** 本帧 MUST 不录制 Draw，pending resources 保持可在后续有效 frame 提交

## Minimal Implementation Example

> Non-normative：本示例只说明推荐的所有权、线程、主调用顺序和失败方向；若与 Type Contracts 或 requirements 冲突，以规范性内容为准。

```text
GT:
copy Camera/viewport values into SceneView
→ move SceneView into SceneViewFamily
→ construct one-shot ForwardSceneRenderer
→ Draw command takes unique SceneRenderer ownership

RT:
begin_frame()
→ create one graphics context
→ record_pending_uploads()
→ ForwardSceneRenderer::init_views()
     → create ViewInfo
     → build finite six-plane or infinite-far five-plane ConvexVolume
→ compute_view_visibility()
     → visible StaticMeshSceneProxy remains in the current ViewInfo result
     → outside StaticMeshSceneProxy is rejected
→ test build records its local deterministic pass using the same ViewInfo result
→ visible proxy contributes frame-local MeshBatch
→ match LocalVertexFactory with ShaderVertexInput
→ resolve View/Material/Object bindings
→ ForwardSceneRenderer::render_base_pass()
→ finish immutable graphics list
→ end_frame()
→ destroy the one-shot SceneRenderer on RT

Failure A:
if init_views() finds invalid input or a degenerate Plane,
skip visibility and every pass, then close the acquired frame with abort_frame().

Failure B:
if one MeshBatch has incompatible ShaderVertexInput,
diagnose and skip that batch without building a partial pipeline; other valid batches may continue.

Failure C:
if terminal skips Draw before execution,
dispose the owned SceneRenderer without reading RenderScene or recording the test/Base Pass.
```
