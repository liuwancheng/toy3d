## Purpose

定义 `engine/runtime/engine.h` 中现有 `toy3d::Engine` 作为 Game Thread composition root 的职责，使 Task Graph、RenderingThread、Renderer、Window/Surface 和 Game loop 具有显式所有权与确定初始化顺序，同时避免新增 Engine 抽象层或把它变成渲染 service locator。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `toy3d::Engine` | 现有修改 | 声明于 `engine/runtime/engine.h`、实现于 `engine/runtime/engine.cpp`；GT-owned composition root；拥有平台、窗口、Task Graph、RenderingThread controller、Renderer shell 与 FrameEndSync；不拥有 RT 可变 RenderScene/RHI device/viewport/resource manager |
| `EngineLifecycleState` | 新增 enum class | `toy3d::Engine` 私有的 GT-only 启停状态；由该对象创建并销毁，不表达 Renderer terminal 细节 |

## ADDED Requirements

### Requirement: Engine 拥有进程级框架对象
现有 `toy3d::Engine` SHALL 在 Game Thread 显式拥有 Task Graph active instance、RenderingThread lifecycle controller、Renderer shell、Window/Surface 和 FrameEndSync，并按依赖逆序销毁。

#### Scenario: Engine 初始化
- **WHEN** Platform、Window 和 Surface 创建成功
- **THEN** Engine MUST 依次启动 Task Graph、RenderingThread 和 Renderer bootstrap，成功后才进入主循环

### Requirement: RT 可变状态不属于 Engine
Engine MUST NOT 直接拥有或调用 RHIDevice、RHIViewportContext、RenderScene、RenderResourceManager 或具体 SceneRendering 的可变执行接口；这些对象 MUST 在 logical Rendering Thread 的 Renderer domain 内创建和销毁。

#### Scenario: 每帧绘制
- **WHEN** Engine main loop 请求一帧渲染
- **THEN** Engine MUST 通过 Game/Render framework 投递输入，而不得直接 begin frame、调用 pass 或 submit RHI work

### Requirement: Engine 不是 service locator
业务模块 MUST NOT 通过全局 Engine getter 查找 Task Graph、Renderer、RenderScene 或 RHI；全局访问只允许由对应基础设施的受控 façade 提供。

#### Scenario: Component 创建 render state
- **WHEN** Component 需要通知 RenderScene
- **THEN** 它 MUST 使用 SceneInterface/RenderCore 专用入口，不得请求 Engine 指针

### Requirement: 不新增第二层 Engine 抽象
实现 MUST 直接修改现有 `engine/runtime/engine.h/.cpp` 中的 `toy3d::Engine`，不得新增 `EngineDomain`、`EngineContext`、`EngineServices`、`RuntimeEngine`、`GameEngine` 或等价 wrapper 来代替其 composition-root 职责。

#### Scenario: composition root 需要新操作
- **WHEN** Task Graph、RenderingThread 或 Renderer 生命周期需要由进程入口编排
- **THEN** 操作 MUST 进入现有 `toy3d::Engine` 或所属基础设施的窄接口，不得创建另一套 Engine 对象

### Requirement: 初始化失败逆序回滚
Engine MUST 保留首个初始化错误并只清理已经成功创建的阶段，回滚完成前不得发布 Running。

#### Scenario: Renderer bootstrap 失败
- **WHEN** Task Graph 与 RenderingThread 已启动但 RHI bootstrap 失败
- **THEN** Engine MUST teardown Renderer domain、join RenderingThread、shutdown Task Graph，再销毁 Window/Surface，并返回原始错误
