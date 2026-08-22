## Purpose

定义 Game Thread 到 logical Rendering Thread 的唯一命令形状、FIFO、producer、inline fallback 和专用 façade 边界，避免额外 route、registry 或 RHI 参数污染普通命令。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RenderCommandTask<Callable>` | 新增内部模板 | Task Graph-owned move-only callable payload；在 logical RT 执行或析构；不公开 Renderer/RHI 参数，不保存 completion |

公共 façade 使用函数模板和现有状态类型，不新增 `Binding`、`Route`、`Registry`、`Token` 或 type-erased command 类型。

## ADDED Requirements

### Requirement: 普通命令签名
普通 RenderCommand MUST 为 `void() noexcept` callable，支持 move-only capture，并由 Task Graph 直接保存具体 callable 类型。

#### Scenario: 独占 payload
- **WHEN** GT 将 unique ownership 移入 RenderCommand
- **THEN** payload MUST 只移动一次，并在 logical RT 执行完成或 terminal skip 时析构

### Requirement: 执行选择
GT+multi-thread SHALL enqueue，logical RT SHALL inline，GT+single-thread SHALL inline；AnyWorker 第一阶段 MUST 被拒绝。

#### Scenario: Worker producer
- **WHEN** AnyWorker 尝试投递普通 RenderCommand
- **THEN** 框架 MUST fail fast 并记录 producer 线程与命令名

### Requirement: 唯一 FIFO transport
RenderCommand MUST 只使用 Task Graph RenderingThread named queue；同一 GT producer 的 Scene、Material、Resource、Draw 和 Fence 命令 MUST 按投递顺序执行。

#### Scenario: 更新先于 Draw
- **WHEN** GT 依次投递资源更新和 Draw
- **THEN** RT MUST 在 Draw 前应用该更新

### Requirement: 窄 façade
框架 SHALL 只暴露 `enqueue_render_command()` 及按领域定义的 Scene/resource 操作，不得提供 `get_renderer()`、`get_render_resource_manager()` 或全局 RHI command list。

#### Scenario: 命令需要资源初始化
- **WHEN** GT 请求初始化 render resource
- **THEN** 调用方 MUST 使用资源 capability 的专用入口，而不是在 callable 中全局查找 manager
