## Purpose

定义 Game Thread 创建一次性 View 输入和 SceneRenderer、Rendering Thread 消费 RenderScene 并录制一帧的流程，避免为跨线程数据引入 Snapshot 命名或长期帧包。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `SceneView` | 新增 struct/class | GT 创建的一次性相机与 viewport value；move 到 RT 后只读；不引用 CameraComponent |
| `ViewFamily` | 新增 struct/class | 聚合一次 Draw 的 SceneInterface、views、output 与 show/config values；一次性 ownership |
| `SceneRenderer` | 新增 polymorphic class | GT 基于 ViewFamily 创建，Draw command 独占并移入 RT；RT 读取 RenderScene、录制 pass、执行后析构 |

不得新增任何以 `Snapshot` 命名的类型。

## ADDED Requirements

### Requirement: GT 构造一次性 View 输入
GT SHALL 从 Camera/Window/Game 状态复制构造 SceneView 与 ViewFamily；构造期间 MUST NOT 读取 RT 可变 RenderScene。

#### Scenario: Camera 在投递后继续变化
- **WHEN** GT 已投递 frame N 的 SceneRenderer 后 CameraComponent 再次更新
- **THEN** frame N MUST 使用已拥有的 view values，不得回读 CameraComponent

### Requirement: SceneRenderer ownership 转移
SceneRenderer MUST 整体 move 进 Draw command，GT 投递后不再访问；logical RT MUST 使用 FIFO 前序更新后的 RenderScene 执行并析构它。

#### Scenario: Draw command 被 terminal skip
- **WHEN** terminal 发生在 Draw 执行前
- **THEN** SceneRenderer MUST 不执行，并在 logical RT command disposal 路径析构

### Requirement: 一帧只录制一个 graphics list
第一阶段每个 viewport Draw SHALL begin frame、创建一个 graphics context、录制 pending uploads 和全部 graphics pass、finish 一个 immutable list 并 end frame。

#### Scenario: 多个业务 pass
- **WHEN** frame 包含 depth、forward 和 composition pass
- **THEN** pass MUST 按显式顺序串行录入同一 context，结果不得依赖并行录制

### Requirement: Recoverable viewport 状态
NotReady、OutOfDate 和 Suboptimal MUST 由 SceneRenderer/Renderer frame policy 处理，不得让 GT 直接操作 swapchain 或 backend token。

#### Scenario: 最小化窗口
- **WHEN** begin frame 返回 NotReady
- **THEN** 本帧 MUST 不录制 Draw，pending resources 保持可在后续有效 frame 提交
