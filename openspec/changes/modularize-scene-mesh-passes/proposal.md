## Why

当前 `ForwardSceneRenderer` 同时承担 View 初始化、可见性计算、MeshBatch 收集、Base Pass draw command 物化与 RHI render-pass 录制，且以仅服务当前实现的 `PreparedBasePass` 聚合数据。这使 pass 级、view 级和 draw 级状态边界不清晰，也会让即将加入的 ShadowPass 继续复制或挤入同一大型实现文件。

## What Changes

- 新增 `renderscene/pass/` 业务 pass 目录，将 BasePass 从 `ForwardSceneRenderer` 拆为独立模块，并为后续 ShadowPass、DepthPass 和 VelocityPass 保留相同的组织方式。
- **BREAKING** 删除私有临时概念 `PreparedBasePass`，以 `MeshPassDrawList` 表达一个 render view 在一个 mesh pass 中的 draw list，以 `MeshDrawCommand` 表达单次 mesh draw；RHI attachment scope 仍由具体 pass 独立持有。
- 将 BasePass 的 mesh filtering、shader/pipeline/binding 物化和录制收敛到明确的 `render_base_pass(...)` 入口；所有 pipeline、binding 与 uniform upload 必须在 `begin_render_pass()` 前完成。
- 在 `renderscene/view/` 中新增 stateless 的 `scene_visibility.h/.cpp` 模块，把 camera view 的 primitive culling 与 MeshBatch gather 从 `ForwardSceneRenderer` 移出；第一阶段不新增长期 `SceneVisibility` owner 或 context 类。
- 收敛 `ViewInfo` 为单帧、单 camera view 的 render-side 状态：保存 canonical matrices/frustum、View uniform 参数与资源、当前帧 visible primitives 和 candidate MeshBatches，不保存具体 pass、attachment、command context 或 backend descriptor 状态。
- 明确 camera visibility 与未来 ShadowPass visibility 相互独立；ShadowPass 可按 light/cascade/cube face 构造自己的 pass inputs 与 `MeshPassDrawList`，不把 shadow 数据伪装成 camera `ViewInfo`。
- 不新增通用 `RenderPass` 基类、临时 Pass Scheduler 或首批通用 `MeshPassProcessor` 基类；具体 pass 使用明确入口，只有多个 pass 出现真实重复后才提取共享实现，未来 RDG 负责资源依赖与调度。

## Capabilities

### New Capabilities

- `game-render-framework/mesh-pass-organization`: 定义具体 mesh pass 的目录与职责、`MeshDrawCommand`/`MeshPassDrawList` contract、BasePass prepare/execute 边界及 ShadowPass 扩展规则。
- `game-render-framework/scene-visibility`: 定义 stateless visibility 模块、camera view 可见性与 MeshBatch gather，以及与未来 shadow visibility 的边界。

### Modified Capabilities

- `game-render-framework/view-render-flow`: 将 visibility 与 BasePass 从 `ForwardSceneRenderer` 私有实现迁出，收敛 `ViewInfo` 的 per-view 数据和 GPU 资源生命周期，并保持单 graphics list 的现有帧顺序。
- `game-render-framework`: 更新端到端帧阶段名称与职责，使总控 contract 与独立 visibility/BasePass 模块一致，同时保留正在演进的 Tonemap/ImGui 输出顺序。

## Impact

- 主要影响 `engine/runtime/renderscene/view/`、新增的 `engine/runtime/renderscene/pass/`、renderscene CMake source 登记以及相关 runtime tests。
- `ForwardSceneRenderer` 将只负责编排 View 初始化、visibility、跨 pass transition 和具体 pass 调用，不再物化单个 MeshBatch 的 pipeline/binding。
- 公共 RHI 接口和五个 logical Binding Group contract 不变；Vulkan 仍由 backend 将 Global+View 聚合到 physical set 0，D3D11/D3D12 保持各自 target mapping。
- 本 change 与 `add-tonemap-imgui-output-passes` 同时修改帧流程；实施时必须在其 BasePass→Tonemap→ImGui 顺序之上合并，不得回退最终输出职责分离。
- `document/rhi-design.md` 中仍描述 `PreparedBasePass`/`MeshDrawPacket` 的段落需要同步为本 change 确认后的 active contract。
