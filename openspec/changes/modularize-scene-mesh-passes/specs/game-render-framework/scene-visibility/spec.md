## Purpose

定义 camera view 的每帧 primitive visibility 与 candidate MeshBatch 收集边界，并将其与具体业务 pass 和未来 shadow visibility 隔离，使可见性算法能够独立演进而不引入无生命周期意义的长期管理对象。

## Type Contracts

本 capability 第一阶段不新增具名类型；它复用 `RenderScene`、`ViewInfo`、`PrimitiveSceneInfo` 与 `MeshBatch`，以 stateless 函数更新调用方拥有的 frame-local `ViewInfo` 结果。未来新增 work packet、context 或 visibility result 类型前，MUST 先在本节登记完整 ownership/thread/error contract。

## ADDED Requirements

### Requirement: Camera visibility 使用 stateless 模块
Camera visibility SHALL 由 `engine/runtime/renderscene/view/scene_visibility.*` 中的 stateless 操作完成。该操作 MUST 只读取 logical RT 当前 `RenderScene`，并更新调用方拥有的当前帧 `ViewInfo` 集合；第一阶段 MUST NOT 新增长期 `SceneVisibility` owner、跨帧 cache、Handle/Token 或隐藏的全局状态。

#### Scenario: 连续两帧复用 Renderer 路径
- **WHEN** 下一帧以新的 `ViewInfo` 集合再次计算 scene visibility
- **THEN** 结果 MUST只由本帧 Scene/View 输入决定，不得依赖模块内部保留的上一帧可变状态

### Requirement: Camera primitive culling 保持既有语义
Camera visibility MUST 对每个 `ViewInfo` 线性遍历仍注册的 `PrimitiveSceneInfo`，排除 visibility 关闭、world bounds 无效、Proxy 缺失或已移除的 primitive，再使用该 View 的 reversed-Z `ConvexVolume` 与 world-space AABB 相交生成仅当前帧有效的 visible primitives。AABB 接触任一 plane MUST 视为可见；第一阶段 MUST NOT 引入 octree、occlusion/HZB、distance culling 或 LOD。

#### Scenario: 同一 Primitive 对两个 View 结果不同
- **WHEN** Primitive bounds 只与两个 camera Views 中一个 View 的 frustum 相交
- **THEN** Primitive MUST只写入对应 `ViewInfo` 的 visible primitives

#### Scenario: 无效 world bounds
- **WHEN** Primitive 的 world-space bounds 包含非有限值或 minimum 大于 maximum
- **THEN** visibility MUST诊断并排除该 Primitive，不得把无效 bounds 送入 frustum test

### Requirement: Candidate MeshBatch 在 visibility 阶段收集
Camera visibility 完成 primitive culling 后，模块 SHALL 从当前 View 的 visible render proxies 收集 frame-local candidate `MeshBatch`，并在每次计算前清空旧 visible primitives 与 MeshBatches。收集只负责形成 pass 可消费的 draw inputs；shader variant、pipeline、logical binding 和具体 pass eligibility MUST由对应 mesh pass 决定。

#### Scenario: 可见 StaticMesh
- **WHEN** 可见 `StaticMeshSceneProxy` 具有可用 `StaticMeshRenderData` 与 Material render representation
- **THEN** visibility 模块 MUST为当前 View 生成 candidate MeshBatch，BasePass随后独立决定其是否可绘制

#### Scenario: 非 StaticMesh Proxy
- **WHEN** 可见 Proxy 当前没有已登记的 MeshBatch producer
- **THEN** 模块 MUST安全跳过且不得构造残缺 MeshBatch

### Requirement: Camera 与 Shadow visibility 相互独立
Camera visibility MUST只写入 camera `ViewInfo`。未来 ShadowPass 的 light/cascade/face visibility MUST使用其调用方拥有的独立输入与结果，不得复用或覆盖 camera visible primitives/MeshBatches，也不得要求 `ViewInfo` 持有 light、cascade、atlas 或 shadow depth attachment。

#### Scenario: Camera 外但 Shadow 内的 caster
- **WHEN** 一个 primitive 不在 camera frustum 内但位于某个 shadow cascade 的 caster volume 内
- **THEN** camera visibility MAY剔除它，而 shadow visibility MUST仍能独立将其选为 caster

## Minimal Implementation Example

以下示例是 non-normative；规范性内容以上述 requirements 为准。

```cpp
// ForwardSceneRenderer owns view_infos for this frame; the visibility module owns no retained state.
compute_scene_visibility(render_scene, view_infos);

for (const ViewInfo& view_info : view_infos)
{
    // A concrete pass observes the frame-local candidates and resolves its own draw commands.
    consume_mesh_batches(view_info.mesh_batches());
}
// 若输入或 bounds validation 失败，调用方终止后续 scene pass 录制并闭合当前 frame。
```
