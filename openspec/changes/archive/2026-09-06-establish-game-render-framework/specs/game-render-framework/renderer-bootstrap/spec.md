## Purpose

定义 logical Rendering Thread 内的渲染器启动引导（bootstrap）：在 Renderer 进入 Running 前，完成 RHIDevice、RenderResourceManager、最小 placeholder GPU resources 与 primary viewport 的一次性全有或全无初始化、发布和失败回滚。

本 capability 中的 bootstrap 只表示 Renderer 启动门槛，不表示加载全部游戏资产、编译全部 Shader 或创建全部场景资源，也不引入 Bootstrap class、manager、service或普通资源同步加载入口。后文统一称“启动引导”。

## Type Contracts

本 capability 不新增独立运行时类型；它编排 `Renderer`、现有 RHI 类型和 `RenderResourceManager`。初始化阶段状态由 `RendererLifecycleState`（归属 terminal/shutdown capability）表达。

Renderer internal domain SHALL独占RHIDevice、RenderResourceManager、placeholder RHI refs、RenderScene和primary RHIViewportContext。GT/Engine只持有Renderer及被明确允许的只读status/command façade，不得直接持有、查询或销毁这些内部对象。

## ADDED Requirements

### Requirement: Bootstrap 顺序固定
RT启动引导 MUST按RHIDevice、RenderResourceManager、placeholder device submission、exact completion wait、primary viewport的顺序执行，全部成功后才发布Renderer Running。Renderer MUST先从Stopped发布Starting；成功后发布Running并开放普通Scene/resource/frame façade，失败则发布Terminal/RendererStatus first error并保持façade关闭。

#### Scenario: 正常启动
- **WHEN** Window/Surface、Task Graph 与 RenderingThread ready
- **THEN** bootstrap MUST 完成 placeholder GPU 可用性和 viewport 创建后才开放 frame/resource façade

#### Scenario: 普通命令过早到达
- **WHEN** RenderingThread已经attach但Renderer仍处于Starting且启动结果尚未发布
- **THEN** 普通Scene/resource/frame façade MUST拒绝enqueue或保持关闭，不得让命令观察部分初始化domain

### Requirement: 启动引导输入与线程固定
composition root SHALL在GT创建Window和backend-independent RHISurface输入，并启动Task Graph/RenderingThread ready handshake；RHIDevice、Manager、placeholder、viewport的创建和可变状态 MUST只在logical RT执行。bootstrap result必须在普通façade开放前发布给composition root，GT不得轮询RT内部对象判断ready。

#### Scenario: Surface 已创建
- **WHEN** logical RT开始Renderer Starting流程
- **THEN** 它 MAY消费composition root提供的RHISurface identity创建device/viewport，但不得读取Window的可变platform/backend internals

### Requirement: Placeholder 全有或全无
placeholder create、upload/transition recording、finish、submit或completion wait任一步失败 MUST导致整个Renderer启动引导失败；不得发布部分placeholder set。所有未发布refs MUST在RT按逆序释放，已进入command list的RHI/staging payload按submit truth与completion规则保活。

#### Scenario: Placeholder submit 失败
- **WHEN** device-level command list 未成功 submit
- **THEN** bootstrap MUST 保留原始错误并释放未发布 refs，Renderer MUST 不进入 Running

#### Scenario: Placeholder completion wait 失败
- **WHEN** placeholder list已成功submit但等待指定completion返回DeviceLost或不可恢复错误
- **THEN** Renderer MUST保留该first error、进入Terminal并执行有限teardown，不得把placeholder发布为GPU可用或继续创建viewport

### Requirement: Bootstrap 使用显式 device context
placeholder MUST使用非viewport device-level graphics context录制upload/transition、finish immutable command list并显式queue submit；resource create MUST不隐式submit或wait。device-level context不属于viewport frame slot，不得acquire/present，也不得绕过command-list local state、queue committed state或completion-driven payload保活。

#### Scenario: Bootstrap 录制
- **WHEN** 创建 placeholder texture
- **THEN** create empty resource、upload、transition、finish、submit、wait MUST 是显式步骤

#### Scenario: Placeholder submit success
- **WHEN** device-level list成功submit并返回completion value
- **THEN** 启动引导 MUST只等待该指定completion，不能把queue-wide wait idle伪装成普通逐帧策略

### Requirement: Placeholder 完成后才创建 primary viewport
primary RHIViewportContext MUST在完整placeholder set成功submit且指定completion确认后创建。viewport不得反向拥有RHIDevice、Manager或placeholder，也不得把swapchain、image index、frame slot、semaphore或fence暴露给Renderer上层。

#### Scenario: Placeholder 未完成
- **WHEN** placeholder submission仍未达到指定completion
- **THEN** Renderer MUST保持Starting且不得创建primary viewport或发布Running

### Requirement: 失败逆序回滚
启动引导failure MUST只销毁已成功创建的对象，顺序为viewport（若有）、placeholder refs、RenderResourceManager、RHIDevice；原始错误必须成为RendererStatus primary error，cleanup error只能作为secondary diagnostic。若submit是否产生GPU work未知，Renderer MUST进入Terminal且不得按“未提交”路径回滚后重试。

#### Scenario: Primary viewport 创建失败
- **WHEN** device 与 placeholder 已成功但 viewport 创建失败
- **THEN** bootstrap MUST 释放 placeholder、清 manager、shutdown device，并返回 viewport 原始错误

#### Scenario: Device 初始化失败
- **WHEN** RHIDevice创建或initialize失败且后续对象尚未创建
- **THEN** 回滚 MUST只释放device partial state并保留原始错误，不得调用尚不存在的manager/placeholder/viewport

### Requirement: 普通资源不复用 bootstrap wait
Renderer Running后普通Mesh/Texture/Material init SHALL通过RenderCommand进入RenderResourceManager PendingUpload，在后续有效viewport frame由 `record_pending_uploads()`录制，并仅在business submit成功后Ready；不得使用启动引导的device context、同步completion wait或queue-wide idle路径。

#### Scenario: Gameplay 加载 Texture
- **WHEN** Texture 在正常运行期 begin init
- **THEN** 调用 MUST fire-and-forget，资源在后续业务 submit 成功后 Ready

## Minimal Implementation Example

> Non-normative：本示例只说明推荐的ownership、GT/RT线程、启动顺序和失败方向；若与Type Contracts或requirements冲突，以规范性内容为准。

```text
GT composition root:
create Window and backend-independent RHISurface
→ start Task Graph
→ start/attach RenderingThread
→ request Renderer startup

Logical RT startup guidance:
RendererLifecycleState::Stopped → Starting
→ create and initialize RHIDevice
→ create Renderer-owned RenderResourceManager
→ create empty placeholder buffers/textures/views
→ create non-viewport device-level RHIGraphicsCommandContext
→ record placeholder uploads and RHIAccess transitions
→ finish immutable command list
→ graphics queue explicit submit
→ wait only for the returned placeholder completion value
→ publish the complete placeholder set
→ create primary RHIViewportContext
→ publish RendererLifecycleState::Running
→ open ordinary Scene/resource/frame command façade

Running resource path:
GT creates a Texture/TextureResource
→ enqueue fire-and-forget init
→ RT registers PendingUpload
→ later valid viewport frame calls record_pending_uploads()
→ business submit success publishes Ready
→ no startup device-context wait is reused.

Failure A — placeholder submit explicitly fails:
keep Renderer non-Running
→ release unpublished placeholder refs
→ clear RenderResourceManager
→ shutdown RHIDevice
→ publish Terminal with the submit error as RendererStatus primary error.

Failure B — placeholder completion wait returns DeviceLost:
do not create viewport or publish placeholders
→ keep DeviceLost as first error
→ perform finite terminal teardown without retrying startup.

Failure C — primary viewport creation fails:
release viewport partial state
→ release published-only-within-startup placeholder refs
→ clear manager
→ shutdown device
→ preserve the viewport creation error over cleanup diagnostics.
```

Batch C SHALL retain only one normal startup-to-Running smoke and one representative placeholder failure/rollback smoke. Complete device/create/upload/submit/wait/viewport failure injection, DeviceLost and shutdown interaction belongs to the concentrated Batch D matrix; this capability MUST NOT create a separate full fixture for every startup step.
