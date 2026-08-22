## Purpose

作为 Toy3d Game/Render 框架的总控 capability，定义跨线程、Scene、资源与 RHI 子 capability 的依赖关系、端到端帧行为、类型治理和分阶段完成门槛，供多次 apply 持续定位工作。

## Capability Map

```text
legacy-rendering-cleanup
→ task-graph-runtime
→ rendering-thread-lifecycle
→ render-command-transport
→ frame-synchronization
→ renderer-scene-ownership
→ engine-composition-root
→ primitive-proxy-lifecycle
→ view-render-flow
→ render-resource-manager
   ├→ static-mesh-resources
   ├→ texture-resources
   └→ material-updates
→ rhi-frame-submission
→ rhi-resource-state
→ renderer-bootstrap
→ renderer-terminal-shutdown
```

## Type Contracts

总控 capability 不新增运行时类型；所有类型 MUST 归属于一个且仅一个子 capability。

## Side Ownership

```text
Game side
├─ engine/runtime/engine.h::toy3d::Engine
├─ World / Actor / Component
├─ Asset 与 MaterialInstance 的 GT state
├─ SceneView / ViewFamily 构造
└─ FrameEndSync

Bridge contracts（无业务可变状态）
├─ RenderCommand / RenderCommandFence
├─ SceneInterface
└─ RenderResource lifecycle façade

Render side
├─ RenderingThread / Renderer
├─ RenderScene / PrimitiveSceneProxy / PrimitiveSceneInfo
├─ SceneRenderer 执行
├─ RenderResourceManager
└─ RHI / backend
```

## ADDED Requirements

### Requirement: 子 capability 是执行边界
每个子 capability SHALL 拥有独立 requirements、类型清单、任务组和完成门槛；下游 capability MUST NOT 在其依赖 capability 未通过验证前成为正式入口。

#### Scenario: 多次 apply 继续执行
- **WHEN** 新一次 apply 启动
- **THEN** 执行者 MUST 从总控依赖图和 tasks 中选择第一个未完成且依赖已满足的 capability

### Requirement: 新增类型必须先登记
实现期间新增的每个第一方 class、struct、enum、alias 或其他具名类型 MUST 先登记在唯一所属子 Spec 的 `Type Contracts`，并说明职责、所有权、可变线程、创建销毁线程、错误语义和为何不能使用已有类型。

#### Scenario: 实现发现缺少类型
- **WHEN** apply 过程中发现当前 Type Contracts 未覆盖所需具名类型
- **THEN** 实现 MUST 暂停该类型的代码落地，先更新对应 Spec 并重新验证 change

#### Scenario: 实现私有辅助类型
- **WHEN** 新增类型只在 `.cpp` 内使用
- **THEN** 该类型仍 MUST 登记；标准库类型、lambda closure 和编译器生成类型不需要登记

### Requirement: 一帧端到端顺序
正常帧 MUST 按 Game updates、一次性 view/renderer 输入、RT FIFO 更新、单帧录制、业务 submit、presentation 与独立 GPU completion 的顺序执行，且 CPU Fence、submit commit 和 GPU completion MUST 保持不同语义。

#### Scenario: 正常 frame N
- **WHEN** Game Thread 完成 frame N 的 World tick、状态更新和 Draw 投递
- **THEN** logical Rendering Thread MUST 在 Draw 前应用 FIFO 更新，成功 submit 后发布资源状态，并由独立 completion 控制 GPU payload 回收

### Requirement: 不建立兼容双轨
迁移 SHALL 在每个阶段保持一个正式入口；旧 transport、旧资源 registry 或旧 frame submit 路径 MUST NOT 与新路径长期同时可用。

#### Scenario: 新 capability 发布
- **WHEN** 一个新 capability 达到完成门槛
- **THEN** 对应旧正式入口 MUST 在同一迁移批次删除或变为不可构建

### Requirement: Game 与 Render 可变状态分侧
除现有 `engine/runtime/engine.h::toy3d::Engine` 作为 GT composition root 外，框架模块 MUST 明确归属 Game side 或 Render side；跨侧 bridge MUST 只表达命令、同步和生命周期 contract，不得拥有任一侧的业务可变状态。

#### Scenario: 新增模块或类型
- **WHEN** apply 需要新增模块或具名类型
- **THEN** 对应 Type Contracts MUST 标明 Game side、Render side 或 stateless bridge，且不得引入新的 Engine 抽象层
