## Purpose

定义 `engine/runtime/engine.h` 中现有 `toy3d::Engine` 作为 Game Thread composition root 的职责：统一编排 Platform、Window/RHISurface、Task Graph、Renderer 稳定外壳、RenderingThread、FrameEndSync 和 Game loop，同时把 RenderScene、RHI 与资源管理的可变状态留在 logical Rendering Thread。该 capability 不新增第二层 Engine 抽象，也不把 Engine 变成渲染 service locator。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `toy3d::Engine` | 现有修改 | 声明于 `engine/runtime/engine.h`、实现于 `engine/runtime/engine.cpp`；GT-owned composition root；拥有 Platform、Window/RHISurface、Task Graph active instance、Renderer 稳定外壳、RenderingThread controller 与 FrameEndSync；不拥有 logical RT 内部的可变 RenderScene、RHIDevice、RHIViewportContext、RenderResourceManager 或 placeholder resources |

本 capability 不新增 Engine 侧生命周期枚举。Engine 初始化和回滚进度由已经成功创建的 owned member 与各阶段返回结果表达，不再建立一套与 `RendererLifecycleState` 重叠的状态机。

## ADDED Requirements

### Requirement: Engine 拥有进程级框架对象
现有 `toy3d::Engine` SHALL 在 Game Thread 显式拥有 Platform、Window/RHISurface、Task Graph active instance、Renderer 稳定外壳、RenderingThread lifecycle controller 与 FrameEndSync，并按依赖逆序销毁。Renderer 稳定外壳 MAY 由 Engine 持有，但其 RT 可变内部对象 MUST 只在 logical Rendering Thread 创建、访问和销毁。

#### Scenario: Engine 初始化
- **WHEN** Engine 开始完整 runtime 初始化
- **THEN** 它 MUST 依次完成日志/文件系统/配置、Platform、Window/RHISurface、Task Graph 与 GameThread attach、Renderer 稳定外壳、RenderingThread start 和 Renderer 启动引导，全部成功后才进入主循环

#### Scenario: Renderer shell 已创建但 RT 尚未 ready
- **WHEN** Engine 已持有 Renderer 稳定外壳但 RenderingThread 尚未完成 attach 和启动引导
- **THEN** 普通 Scene/resource/frame façade MUST 保持关闭，Engine 不得通过稳定外壳读取或修改尚未发布的 RT 内部状态

### Requirement: 初始化顺序在 single-thread 与 multi-thread 间一致
`RenderingThreadMode::SingleThread` 与 `RenderingThreadMode::MultiThread` MUST 使用相同的 composition、Renderer 启动引导和失败回滚顺序；模式差异只决定 logical Rendering Thread 是否拥有独立 OS thread，不得形成第二套 Renderer/RHI 初始化路径。

#### Scenario: Single-thread 启动
- **WHEN** Engine 选择 `RenderingThreadMode::SingleThread`
- **THEN** Renderer 启动引导 MUST 在已 attach 的 Game Thread 上以 logical Rendering Thread 身份执行，并产生与 multi-thread 路径语义等价的 ready 结果

#### Scenario: Multi-thread 启动
- **WHEN** Engine 选择 `RenderingThreadMode::MultiThread`
- **THEN** Engine MUST 等待 OS Rendering Thread attach 和 Renderer 启动引导结果，不得在 ready 前进入主循环

### Requirement: RT 可变状态不属于 Engine
Engine MUST NOT 直接拥有或调用 RHIDevice、RHIViewportContext、RenderScene、RenderResourceManager、placeholder resources 或具体 SceneRendering 的可变执行接口；这些对象 MUST 在 logical Rendering Thread 的 Renderer domain 内创建和销毁。

#### Scenario: 每帧绘制
- **WHEN** Engine main loop 请求一帧渲染
- **THEN** Engine MUST 通过 Game/Render framework 投递一次性 frame/view 输入与 Draw，而不得直接 begin/end RHI frame、调用 render pass、提交 command list 或查询 RenderScene

### Requirement: Game loop 只负责跨域编排
主循环 SHALL 按 Window event processing、Game/World update、本帧一次性渲染输入构造、Draw 投递和 `FrameEndSync::sync_frame()` 的顺序协调一帧。Game/World mutable objects MUST 留在 GT；投递给 logical RT 的对象 MUST 遵守各自的 copied-value、owned payload 或显式 non-owning lifetime contract。

#### Scenario: 正常帧
- **WHEN** Window 未请求关闭且 Renderer 为 Running
- **THEN** Engine MUST 先完成 GT update，再投递本帧渲染输入和 Draw，最后执行 FrameEndSync；FrameEndSync 只同步 RT CPU 到达，不得被当作 GPU completion

#### Scenario: Renderer terminal
- **WHEN** FrameEndSync 或其他只读状态观察到 Renderer terminal
- **THEN** Engine MUST 停止产生新的普通 render work 并进入有限关闭流程，不得尝试通过 Engine 重启同一个 Renderer object

### Requirement: Engine 不是 service locator
业务模块 MUST NOT 通过全局 Engine getter 查找 Task Graph、Renderer、RenderScene、RenderResourceManager 或 RHI；跨域访问只允许由对应基础设施的受控 façade 提供。Engine 也 MUST NOT 暴露返回这些可变对象的 getter。

#### Scenario: Component 创建 render state
- **WHEN** Component 需要通知 RenderScene
- **THEN** 它 MUST 使用 SceneInterface/RenderCore 专用入口，不得请求 Engine、Renderer 或 RenderScene 指针

### Requirement: 不新增第二层 Engine 抽象
实现 MUST 直接修改现有 `engine/runtime/engine.h/.cpp` 中的 `toy3d::Engine`，不得新增 `EngineDomain`、`EngineContext`、`EngineServices`、`RuntimeEngine`、`GameEngine` 或等价 wrapper 来代替其 composition-root 职责。

#### Scenario: Composition root 需要新操作
- **WHEN** Task Graph、RenderingThread 或 Renderer 生命周期需要由进程入口编排
- **THEN** 操作 MUST 进入现有 `toy3d::Engine` 或所属基础设施的窄接口，不得创建另一套 Engine 对象

### Requirement: 初始化失败逆序回滚
Engine MUST 保留首个初始化错误，只清理已经成功创建的阶段，并按 Renderer domain、RenderingThread、Task Graph、Window/RHISurface、Platform 的依赖逆序回滚。回滚完成前不得进入主循环或发布可运行状态；cleanup error 只能作为 secondary diagnostic，不得覆盖原始错误。

#### Scenario: Renderer 启动引导失败
- **WHEN** Task Graph 与 RenderingThread 已启动但 Renderer 启动引导失败
- **THEN** Engine MUST 让 logical RT 回滚 Renderer domain、request return 并 join RenderingThread、shutdown Task Graph，再销毁 Window/RHISurface 与 Platform，并返回原始错误

#### Scenario: RenderingThread attach 失败
- **WHEN** Task Graph 已创建但 RenderingThread 无法 attach
- **THEN** Renderer 启动引导 MUST 不执行，Engine MUST 回收部分线程状态、shutdown Task Graph 并销毁后续不再需要的 Window/RHISurface 与 Platform

### Requirement: 正常关闭遵守 Game-to-Render ownership 顺序
正常退出 MUST 先停止新的 Game/Render producers，再让 World 执行 `unbind_scene()`、PrimitiveComponent 销毁 render state 并投递 Proxy/Material/TextureResource release；`RenderCommandFence` drain 后才能执行最终 Renderer teardown。Renderer teardown 完成后才能 request RenderingThread return、join RenderingThread、shutdown Task Graph，最后销毁 Window/RHISurface 与 Platform。

#### Scenario: Window 请求关闭
- **WHEN** main loop 观察到 Window close request 且 Renderer 仍健康
- **THEN** Engine MUST 执行正常 drain 与逆序 teardown，不得直接销毁 Window、RenderingThread 或 Renderer 内部对象

#### Scenario: 正常关闭存在 pending render ownership
- **WHEN** FIFO 中仍有 Proxy remove、resource release 或 Draw ownership payload
- **THEN** Engine MUST 等待 logical RT 正常执行和析构这些 payload，再启动最终 Renderer teardown

## Minimal Implementation Example

> Non-normative：本示例只说明推荐的 ownership、GT/RT mutable thread、主调用顺序和失败方向；若与 Type Contracts 或 requirements 冲突，以规范性内容为准。

```text
GT startup:
Engine initializes logging, files and configuration
→ create Platform
→ create Window and backend-independent RHISurface
→ create Task Graph active instance and attach GameThread
→ create Renderer stable shell
→ create/start RenderingThread controller
→ logical RT performs Renderer startup guidance
→ wait for the published ready result
→ enter main loop only after RendererLifecycleState::Running

Normal frame on GT:
Window::process_events()
→ update Game/World mutable state
→ construct copied/owned one-frame SceneViewFamily input
→ enqueue Draw through the RenderCommand façade
→ FrameEndSync::sync_frame()
→ never interpret the CPU fence as GPU completion

Logical RT frame:
consume the owned Draw payload
→ SceneRenderer/ForwardSceneRenderer executes the Renderer and RHI path
→ destroy the one-frame SceneRenderer payload on logical RT

Normal shutdown on GT:
stop new Game/Render producers
→ World::unbind_scene()
→ PrimitiveComponent::destroy_render_state()
→ enqueue Proxy/MaterialRenderProxy/TextureResource releases
→ RenderCommandFence drains accepted FIFO work
→ enqueue final Renderer teardown
→ wait for logical RT teardown result
→ request return and join RenderingThread
→ shutdown Task Graph
→ destroy Window/RHISurface and Platform

Failure — Renderer startup guidance fails:
preserve the original startup error
→ logical RT rolls back only created Renderer-domain objects
→ return/join RenderingThread
→ shutdown Task Graph
→ destroy Window/RHISurface and Platform
→ do not enter the main loop

Failure — FrameEndSync observes Renderer terminal:
stop new ordinary render commands
→ retain RendererStatus first error
→ use the finite terminal shutdown path
→ do not restart the same Renderer object through Engine.
```

Batch B SHALL retain only one normal Engine composition/startup/frame/shutdown smoke and one representative startup failure rollback smoke. The complete single/multi-thread equivalence, per-stage initialization failure, Renderer terminal, DeviceLost and pending-ownership matrix belongs to concentrated Batch D testing; this capability MUST NOT introduce a separate full fixture for every Engine stage.
