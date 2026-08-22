# Toy3d Game/Render Thread 总体设计

> **已被替代：** 本文不再构成当前 Game/Render Thread contract。唯一总体入口为 `document/game-render-thread-framework-design.md`。

## 1. 文档状态与职责

本文是 Toy3d Game Thread、Rendering Thread、RenderScene 与 RHI 命令边界的**唯一入口文档**。当前状态为
**设计定型稿**：记录本轮已经确认的 Game/Render Thread、SceneProxy、ViewFamily 与帧同步 contract，作为分批实施依据。
RHI 深层录制、批次与后端映射仍由后续 RHI 专项设计确认；本文只固定 `RHIDeviceCommandList` 的最小边界。

本文统一包含总体架构、RenderCommand stream、帧同步、`RHIDeviceCommandList` 与资源生命周期。现阶段不再拆分
Game/Render 专项子文档；只有某部分进入独立实现、测试矩阵和维护责任已经稳定，且本文篇幅实际影响评审时，才允许拆出
子文档。拆分后本文仍必须保留入口、决策摘要和规范性链接。

相关文档只提供相邻模块 contract：

- `gamescene-design.md` 继续负责 World、Actor、Component、注册与 GameScene 生命周期；
- `threading-task-graph-design.md` 继续负责共享 Task Graph，不承载渲染领域策略；
- `rhi-design.md` 继续负责公共 RHI、command context、recorded command list、queue 与 viewport contract。

若旧 `rendering-engine-foundation-design.md` 中的 `RenderFramePacket/Dispatcher/Queue/Completion` 与本文冲突，
以本文为准。实施完成后应删除被取代的旧章节，而不是长期保留两套规范。

## 2. 目标与非目标

### 2.1 目标

- 使用 UE4.27 易识别的 Game/Render 职责划分与 RenderCommand 思想；
- Game Thread 与 Rendering Thread 通过 Task Graph named queue 有序通信；
- RenderScene 是 Rendering Thread 独占的持久镜像，不逐帧复制整个 World；
- Scene、Proxy、View、RenderResource 和 RHI 对象具有明确线程归属与生命周期；
- 资源 init、update、release 通过 RenderCommand 进入 Rendering Thread；
- CPU command completion、RHI queue submission 和 GPU completion 使用不同同步语义；
- multi-thread 与 single-thread fallback 共享同一逻辑命令路径；
- 迁移结束后删除旧 transport，不保留长期兼容双轨。

### 2.2 非目标

- 不引入 RHI Thread，当前及可预见设计均不为其预留软件命令回放层；
- 不复制 UE 的 `FRHICommandBase` 单链表、command arena、bypass 或 `ExecuteAndDestruct()`；
- 不引入 RDG、async compute、multi-queue 或 Pass 内并行；
- 不引入 UObject、GC、反射或 UE 类型前缀和宏；
- 不建立万能 `RenderThreadContext`；
- 不让 Task Graph 理解 Scene、frame lag、viewport、RHI 或 Renderer shutdown 规则；
- 不以一次性全局 frame packet 作为 Game/Render 的唯一通信单元。

## 3. 总体分层

```text
Game Thread

World / Actor / Component
    │ SceneInterface bridge methods
    │ owned value / non-owning Proxy identity / transferred ownership
    ▼
Task Graph RenderingThread named queue
    │ ordered RenderCommand stream
    ▼
Rendering Thread

Renderer（渲染系统 owner）
├── Scene final : SceneInterface
│   └── SceneInfo / SceneProxy / persistent RenderScene state
├── RenderResource ownership and cache
├── RHIDeviceCommandList（最小 device-level 命令入口）
├── ViewFamily-specific DrawSceneCommand
└── RHIViewportContext / SceneRenderer
        │
        ▼
公共 RHI
├── RHIGraphicsCommandContext
├── RHICommandList（finish 后的提交载荷）
├── RHIQueue
└── Vulkan / D3D11 / D3D12 backend
```

这里有两条不同的有序链：

1. Game → Render 的顺序由 Task Graph 的 RenderingThread named queue 保证；
2. Render → GPU 的顺序由 command context 录制、graphics queue 实际提交顺序保证。

不得用第二条软件链重复缓存第一条已经有序的 RenderCommand，也不得把 GPU completion 混入 CPU named queue fence。

## 4. 已确认原则

| ID | 状态 | 决策 |
|---|---|---|
| GRT-001 | 已确认 | `RenderingThread` 只是 Task Graph named queue 的执行线程，不拥有 Scene/RHI 业务策略。 |
| GRT-002 | 已确认 | `Renderer` 是渲染系统 composition owner；不增加万能 `RenderThreadContext`。 |
| GRT-003 | 已确认 | 具体 `Scene` 自身实现小型 `SceneInterface`；Game 方法只 enqueue，Render 方法才修改持久 Scene state。 |
| GRT-004 | 已确认 | Component 在 Game Thread 创建真正的 SceneProxy；Component 保存 non-owning `SceneProxy*`，RenderCommand 转移独占所有权，最终由 Scene/SceneInfo RAII 持有。 |
| GRT-005 | 已确认 | 第一阶段只允许 Game Thread 提交正式 RenderCommand，保持单 producer 语义。 |
| GRT-006 | 已确认 | 每个 ViewFamily 独立提交 `DrawSceneCommand`，不再使用全局大帧包。 |
| GRT-007 | 已确认 | `FrameEndSync` 使用双 CPU fence 支持 one-frame lag；GPU completion 独立处理。 |
| GRT-008 | 已确认 | single-thread 在 Game Thread 立即执行相同 RenderCommand callable；Gameplay 仍不得绕过 RenderCommand/RHI 边界。 |
| GRT-009 | 已确认 | 旧 `RenderFramePacket/Dispatcher/Queue/Completion` 模式最终删除，不保留长期兼容双轨。 |
| GRT-010 | 已确认 | 不引入 RHI Thread，因此不复制 UE 的软件 RHI command 单链表。 |
| GRT-011 | 已确认 | 新建 public RHI 的最小 `RHIDeviceCommandList` 结构，作为所有 RenderCommand 的统一参数；更深 RHI 行为后置。 |
| GRT-012 | 已确认 | 保留现有提交载荷名称 `RHICommandList`，不改名为 `RHIRecordedCommandList`。 |
| GRT-013 | 已确认 | 全局 `enqueue_render_command()` 只是一层 UE 式模板 façade，直接构造现有 Task Graph 的专用 GraphTask，不建立第二条 queue/dispatcher。 |
| GRT-014 | 已确认 | RenderingThread/GameThread named task 动态增长，不受固定 4096 outstanding budget 限制；`AnyWorker` 继续使用 bounded budget。 |
| GRT-015 | 已确认 | 普通 DrawSceneCommand 无独立 completion；FrameEndSync 直接复用 Task Graph GraphEvent。 |

## 5. 所有权与生命周期总表

| 对象 | 创建线程 | 所有者 | 可变线程 | 销毁边界 |
|---|---|---|---|---|
| `World/Actor/Component` | Game | `World`/Actor | Game | Game 生命周期安全点 |
| `Scene final : SceneInterface` | composition root | `Renderer` | Game 只调用 façade；state 仅 Rendering 可变 | 停止生产并 drain RenderCommand 后 |
| `SceneProxy` | Game 创建 | add command 转移给 `SceneInfo` | Game 只调用明确的 enqueue façade；state 仅 Rendering 可变 | remove command 执行后，GPU 引用另行保活 |
| `PrimitiveSceneInfo` | Rendering | `Scene` | Rendering | 从 Scene 移除后 |
| `RHIDeviceCommandList` | Rendering | `Renderer` | Rendering | pending work 提交或丢弃后 |
| `RHIGraphicsCommandContext` | Rendering | 当前 device/frame recording scope | 单一录制线程 | finish/discard 后 |
| `RHICommandList` | finish 时产生 | submission/frame slot | immutable | queue completion 后 |
| `SceneViewFamily` | Game | `DrawSceneCommand` | 构造后 immutable | DrawSceneCommand 执行后 |
| `SceneRenderer` | Rendering | 当前 DrawSceneCommand | Rendering | 当前 draw 请求结束后 |
| `RenderViewport` | 引擎初始化阶段 | `Renderer` | Rendering | 停止对应 draw、drain 后 |
| `RHIViewportContext` | Rendering | Renderer/RenderViewport | Rendering | frame 收尾且 GPU 安全后 |

Game Thread 命令不得捕获生命周期不覆盖执行点的 World、Actor、Component 或临时裸引用。跨线程数据使用：

- owned value snapshot；
- 稳定 `Scene*`、`SceneProxy*`、`RenderViewport*` non-owning identity，且由 shutdown/drain contract 保证生命周期；
- `unique_ptr` 所有权转移；
- 明确不可变且生命周期充足的共享资源版本。

SceneProxy 不得保存可在 Rendering Thread 解引用的 Component、Actor、World 或其他 Game 对象指针。Component unregister
时先清空自身 non-owning Proxy 指针，再提交 remove；由于 Proxy 不回访 Game 对象，Component 可以立即销毁，不建立
per-Component detach fence。

## 6. 帧与数据更新模型

不再把“帧”定义为一个包含全部场景变更、资源变更、View 和 completion 的总 packet。逻辑帧由有序命令组成：

```text
Add/Update/Remove Scene commands
Resource init/update/release commands
DrawSceneCommand(ViewFamily A)
DrawSceneCommand(ViewFamily B)
Frame fence command
```

- 持久状态使用增量命令更新；
- ViewFamily 是某次观察请求的 command-owned value snapshot，由对应 `DrawSceneCommand` 独占；
- 同一 Game frame 内可有零到多个 DrawSceneCommand；
- fence 只标记此前 Rendering Thread CPU 命令到达点；
- device-level RHI work 必须在规定边界提交，但不要求 GPU 在 fence 时完成。

## 7. 模块与依赖方向

已确认职责落点（具体文件拆分可在实现时按现有 target 调整）：

```text
engine/runtime/rendercore/
├── scene_interface.*
├── render_command.*
├── frame_end_sync.*
└── render_resource.*

engine/runtime/renderscene/
├── rendering_thread.*
├── renderer.*
├── scene.*
├── scene_proxy.*
└── scene_renderer.*

engine/runtime/drivers/rhi/
├── rhi_command_context.*
├── rhi_command_list.*
├── rhi_queue.*
└── rhi_device_command_list.*         # 本轮只建立最小结构，深层行为后置
```

依赖方向：

```text
GameScene → RenderCore bridge contract
RenderScene → RenderCore + public RHI
RenderCore → Core TaskGraph（接口注入）
public RHI → Core，不依赖 RenderScene/GameScene
backend → public RHI
```

是否新建或调整 CMake target 必须在接口定型时结合现有 target 盘点，不在本讨论归档稿中预先制造空 target。

## 8. 错误、平台与安全边界

- `enqueue_render_command()` 只允许 Game Thread 调用；错误线程、初始化前、停止接受后调用均为 contract violation 并 fail fast；
- RenderCommand callable 不返回逐命令 result，不提供异步 completion，必须为 `noexcept`；
- 普通 Scene apply 的 duplicate/unknown 等不变量破坏必须诊断并在 Debug fail fast，不回传 Gameplay 结果；
- 已接受 FrameEndFence 必须完成或发布 terminal renderer 结果，不能永久悬空 waiter；
- RHI 录制或 submit 失败不得发布未成功提交的 resource final state；
- `DeviceLost/BackendFailure` 进入 renderer terminal path，禁止继续复用状态不明对象；
- Vulkan/D3D12 录制原生命令列表，D3D11 由后端保持串行等价语义；
- 公共接口不暴露 `Vk*`、`ID3D*`、native fence、queue family 或 descriptor 类型；
- named-thread RenderCommand 不设固定数量上限，不允许因 4096 budget 丢弃；分配失败属于 fatal；
- terminal renderer failure 后，后续 RenderCommandTask 仍由 Task Graph FIFO 取出，但只析构 payload，不再调用 callable；
- 正常 shutdown 先停止生产并 drain；terminal failure 才跳过 pending callable，随后在 Rendering Thread teardown。

## 9. 分批迁移顺序

### 阶段 A：设计定型

1. 将本文已确认 contract 同步回 GameScene、Task Graph 和 rendering foundation 文档；
2. 盘点旧 frame transport、snapshot Proxy 与 resource batch 调用链；
3. 建立可独立验证的删除顺序，不并行维护新旧正式入口；
4. RHI 仅增加最小 `RHIDeviceCommandList` 结构，深层职责另行设计。

### 阶段 B：Scene bridge

1. 让稳定 `Scene` 自身实现 `SceneInterface`；
2. 组件在 Game Thread 创建类型化 Proxy，register/update/unregister 生成 RenderCommand；
3. SceneInfo 在 Rendering Thread 接管、更新和移除 Proxy；
4. 完成纯 CPU、single-thread 路径测试。

### 阶段 C：Rendering Thread 与同步

1. 引擎初始化阶段创建并 attach RenderingThread，成功后才开放 RenderCommand；
2. 用 named queue 建立唯一 RenderCommand stream；
3. 实现双 `FrameEndSync`、flush、fatal 和 shutdown；
4. 删除旧私有 thread/queue/completion transport。

### 阶段 D：RHI 资源命令

1. 接入 device-level command list/batch；
2. 实现资源 init/update/release 与 draw 前提交边界；
3. 验证 upload-before-draw、状态提交和延迟销毁；
4. 删除旧资源 collector 或隐式 submit 路径。

### 阶段 E：ViewFamily 与正式渲染入口

1. 每个 ViewFamily 独立 `DrawSceneCommand`；
2. viewport frame 与 device-level resource submission 正确排序；
3. multi-thread/single-thread、resize、minimize、failure、shutdown 验收；
4. 删除剩余全局 frame packet 正式入口。

## 10. 删除旧模式的条件

以下条件全部满足后删除旧 transport，不保留 alias 或 adapter：

1. Scene add/update/remove 只通过 RenderCommand stream；
2. ViewFamily draw 只通过 `DrawSceneCommand`；
3. 资源 init/update/release 只通过 Rendering Thread 的正式资源命令路径；
4. multi-thread 与 single-thread 共用相同逻辑入口；
5. frame lag、flush、fatal 和 shutdown 测试通过；
6. 所有旧调用方完成迁移；
7. 文档、测试和源码中旧 `RenderFramePacket/Dispatcher/Queue/Completion` 正式术语零残留。

## 11. 总体验收矩阵

- FIFO：Scene、resource、draw、fence 的执行顺序与提交顺序一致；
- 所有权：命令延迟执行时不访问已销毁 Game 对象；
- Proxy：add/update/remove 与重复、失序、失败路径可诊断；
- frame lag：lag on/off 均无死锁且不会无限领先；
- single-thread：不走 Gameplay→RHI 旁路；
- resource：upload-before-draw、replacement、release、GPU in-flight 保活；
- viewport：NotReady、OutOfDate、Suboptimal、abort 和 terminal error；
- shutdown：正常 drain、初始化失败、render fatal、窗口先关闭等路径都能结束；
- 后端：Vulkan、D3D11 FL11_0、D3D12 和 `VulkanPortable v1` 的公共 contract 可实现。

## 12. 当前后置问题

以下内容不阻塞 Game/Render Thread 第一批实施，不在本文提前定型：

1. `RHIDeviceCommandList` 的完整录制、dispatch、batch/staging 阈值与错误 API；
2. frame-local upload 与 device-level upload 的最终边界；
3. resource release 与 pending RHI work、completion/deferred deletion 的最终交互；
4. D3D11 deferred context 与 backend immutable packet 的选择；
5. Mesh/Material/Texture 的正式 immutable render data 与 RenderProxy contract；
6. RHI map/readback、buffer view、storage binding、resolve 与 GPU fence 等尚未闭环能力。

这些问题进入后续 RHI/RenderResource 专项设计；本轮实施不得以临时正式接口抢先固化答案。

## 13. RenderCommand 与 Scene bridge 详细约束

### 13.1 RenderCommand 的层级

RenderCommand 是 Game → Render 的高层 CPU 命令，不等于 RHI recorded command list 或 GPU command buffer。
第一阶段只有 Game Thread 是正式 producer。multi-thread 模式直接使用 Task Graph 的 RenderingThread named queue；
single-thread 模式在 Game Thread 立即调用同一个 callable。全局 façade 只构造专用 GraphTask，不拥有第二条 queue、
dispatcher、completion 或调度状态机。

概念接口为：

```cpp
enqueue_render_command(
    "CommandName",
    [](RHIDeviceCommandList& device_command_list) noexcept
    {
        // Render Thread work.
    });
```

`enqueue_render_command()` 是模板入口，专用 task 直接存储具体 callable 类型，因此支持 `unique_ptr` 等 move-only capture，
不使用 `std::function` 或公共 type-erased `RenderCommand` 对象。callable 必须满足
`void(RHIDeviceCommandList&) noexcept`；不返回逐命令 result，不用异常传播错误。错误线程、错误生命周期和不满足签名均
fail fast。multi-thread 使用 `FireAndForget`，enqueue 的正常路径必须被 Task Graph 接受；named task 不受 fixed worker budget
拒绝。OOM 属于 fatal。

引擎初始化阶段已经完成 Task Graph、RenderingThread、Renderer/RHI 与全局 façade binding；正常运行代码不为每次 enqueue
增加启动状态分支。每个进程只允许一个 active Renderer/RenderCommand binding。初始化顺序错误、停止接受后 enqueue 或
非 Game Thread 调用均为 contract violation。

命令 payload 允许：

- stable typed ID；
- owned value snapshot；
- `unique_ptr` 所有权转移；
- 明确 immutable 且生命周期充足的共享资源版本。

命令禁止捕获 World、Actor、Component、临时对象裸引用，或可能被 Game Thread 并发修改的资产内部地址。稳定 Scene、Proxy、
RenderViewport 指针只能按本文生命周期 contract 用作 identity。terminal renderer failure 后，Task Graph 仍按 FIFO 取出
pending task，但 task 只析构 payload，不再调用 callable。

### 13.2 SceneInterface 与 Proxy

`Scene` 是稳定对象并直接实现 `SceneInterface`。Game 方法只构造/投递命令，`*_render_thread()` 方法才修改 Scene state。
RenderCore contract 不反向依赖 GameScene，因此 `SceneInterface` 接收已经创建好的 Proxy，不接收 `PrimitiveComponent*`。

调用关系：

```text
PrimitiveComponent register（Game）
    → Component::create_scene_proxy()
    → SceneInterface::add_primitive(unique_ptr<SceneProxy>)
    → enqueue RenderCommand
    → Scene::add_primitive_render_thread（Render）
    → SceneInfo 接管 Proxy

Transform/state change（Game）
    → SceneInterface::update_primitive_*(SceneProxy*, owned value)
    → enqueue RenderCommand
    → Scene 在 Render 侧更新 Proxy 与 dirty collections

PrimitiveComponent unregister（Game）
    → 保存 SceneProxy* 并先清空 Component.scene_proxy_
    → SceneInterface::remove_primitive(SceneProxy*)
    → Scene 在 Render 侧移除 Info/Proxy
```

- Proxy 是 Component 的持久、类型化 Rendering Thread 镜像，不是 snapshot wrapper；不同 Primitive 类型可创建不同 Proxy；
- Proxy 在 Game Thread 构造时复制全部渲染所需数据，不得创建 RHI 资源或读取 Rendering Thread 状态；
- Component 保存 non-owning `SceneProxy*`；add command 转移独占所有权，SceneInfo/Scene 使用 RAII 最终持有；
- Proxy 不保存可在 Rendering Thread 解引用的 Component/Actor/World 指针；调试名称和 ID 也必须复制为 owned value；
- Proxy-local 属性可使用严格成对的 `*_game_thread()` enqueue façade 与 `*_render_thread()` mutation；
- transform、bounds、mesh/material replacement 等影响空间结构或 Renderer cache 的更新必须通过 SceneInterface；
- duplicate add、unknown update/remove 与 Scene shutdown 后调用属于不变量破坏，必须诊断并 fail fast，不建立逐操作回执；
- remove 后，已录制 GPU 工作仍由 `RHICommandList` 与 queue completion 保活。

### 13.3 更新合并

属性 setter 是变化入口并立即 enqueue，不在 Game Thread 重建统一 frame-local dirty batch。Rendering Thread 执行每条命令，
再按 Proxy 与更新类别合并真正昂贵的空间结构、bounds、visibility、draw cache 等 Scene work；后写可以覆盖同一 Proxy 的前写。

```text
Game: SetTransform(A) → SetTransform(B) → SetTransform(C)
Render FIFO: pending_transform[proxy] 最终为 C
Scene update boundary: 只执行一次昂贵的关联更新
```

- 每条 RenderCommand 都保持 FIFO，不因合并丢失 add/update/remove 的生命周期顺序；
- remove 清除该 Proxy 尚未消费的 pending Scene work；
- Render Thread 不扫描 World 发现变化；
- 不为 Scene 更新设置固定命令数量预算；内存耗尽为 fatal。

## 14. ViewFamily、FrameEndSync 与 single-thread

### 14.1 ViewFamily

ViewFamily 是一次观察请求，不是包含全局 Scene/resource 状态的大帧包：

```text
GameViewport / EditorViewport
    → command-owned SceneViewFamily value snapshot
    → DrawSceneCommand(view_family)
    → dispatch pending device commands
    → temporary SceneRenderer
    → viewport Prepare / Record / Submit / Present
```

一个 Game frame 可以有零到多个 ViewFamily。Game Thread 将 Camera 的 view/projection、origin、viewport rect 等当次值复制进
ViewFamily，并把整个 payload 移入 DrawSceneCommand；命令不得回访 CameraComponent、GameViewport 或其他可变 Game 对象。
ViewFamily 使用稳定 non-owning `Scene*` 与 `RenderViewport*` 指定持久场景和输出，二者生命周期由停止生产与 drain 保证。
SceneRenderer 只在 Rendering Thread 为当前 DrawSceneCommand 临时创建并在请求结束后销毁。

普通 DrawSceneCommand 使用 `FireAndForget`，没有独立 completion。screenshot、readback 等确实需要结果的操作使用各自专用
completion，不污染普通 ViewFamily。

### 14.2 FrameEndSync

`RenderCommandFence` 直接使用一个 `TrackSubsequents` RenderingThread GraphTask 的 GraphEvent，不增加 condition variable 或
第二套 event。fence task 在返回前先处理本轮 `RHIDeviceCommandList` 所需的最小 dispatch 边界。两个 CPU fence 按 frame 轮转：

```text
submit Game frame N commands
enqueue Fence[N % 2]

one-frame lag enabled  → wait Fence[(N - 1) % 2]
one-frame lag disabled → wait Fence[N % 2]
```

one-frame lag 开启时，Game frame 0 只 enqueue `Fence[0]` 而不等待；从 frame 1 开始始终等待上一 frame 的 fence。

fence 完成前，必须先 dispatch 此前 pending device-level RHI work。完成只保证此前 RenderCommand 已执行且产生的 RHI work
已经 submit，不保证 GPU 已完成、present 已显示或 deferred deletion 已回收。GPU completion 继续使用
`RHIQueueCompletionValue`、frame slot 或显式 `RHIGPUFence`。

### 14.3 Single-thread fallback

- `get_render_thread()` 返回 `NamedThread::GameThread`；
- `enqueue_render_command()` 立即调用相同 callable，不建立同线程队列或额外 pump 点；
- Scene/RHI 仍遵守逻辑 Rendering Thread domain；
- Gameplay 不直接调用 Scene internal apply 或 RHI；
- resource dispatch、fence、错误和 shutdown 使用同一代码路径。

## 15. RHIDeviceCommandList 最小边界

### 15.1 UE 参考边界

UE 的 `ENQUEUE_RENDER_COMMAND` named queue 与 `FRHICommandBase::Next` 软件 RHI command chain 是两条不同的链。
后者通过 arena、尾插单链表和 `ExecuteAndDestruct()` 服务 RHI Thread、bypass 与并行翻译。

Toy3d 不引入 RHI Thread，已有以下顺序层：

```text
RenderingThread named queue FIFO
    → RHIGraphicsCommandContext recording order
    → RHIQueue actual submit order
```

因此不增加 `CommandNode::next`、command arena、软件 replay 或 bypass。D3D11 所需 deferred context/immutable packet
属于 backend 的 command context/recorded list 实现，不上升为公共软件命令链。

### 15.2 已确认类型边界

| 类型 | 职责 |
|---|---|
| `RHIDeviceCommandList` | public RHI 的最小 device-level 命令入口；本轮只建立结构与 RenderCommand 参数边界 |
| `RHIGraphicsCommandContext` | 单线程后端命令录制接口 |
| `RHICommandList` | finish 后不可修改、可提交且保活 GPU payload 的载荷；保留现有名称 |
| `RHIQueue` | 串行 submit、推进 resource state 和 completion value |
| `RHIFrameContext` | viewport begin/end 内的 presentation image 与 frame-local recording scope |

不采用 `RHICommandListImmediate`：没有 RHI Thread/deferred 模式作为对照，Vulkan/D3D12 仍是录制，同时会与 D3D11
native immediate context 混淆。

### 15.3 本轮实施限制

本轮只建立 `RHIDeviceCommandList` 类型、public RHI 落点以及统一 RenderCommand 参数，不提前固化完整 API、内部 state machine、
batch/staging 阈值、自动 dispatch、release、frame-local upload 或 D3D11 packet 选择。它不得理解 TaskGraph、Scene、ViewFamily、
Viewport 或 Renderer shutdown，也不得演变成万能服务容器。

资源不存在额外的通用 GPU `Ready` 状态；是否可用于后续命令由 RHI 引用发布、RenderCommand 顺序和后续 RHI 专项 contract
共同决定。`Found/Placeholder/Missing` 等资源解析状态只表达业务 fallback，不表达 GPU completion。

## 16. RHI 后置设计入口

后续 RHI 专项必须在 Vulkan、D3D11 FL11_0、D3D12 与 `VulkanPortable v1` 上共同评估：

- `RHIDeviceCommandList` 的录制、finish、submit、discard 与错误 API；
- device-level 与 frame-local work 的使用边界；
- upload staging、分批策略与 completion 生命周期；
- replacement/release、RHI 引用发布与 deferred deletion；
- D3D11 的串行等价实现；
- 与现有 command-list-local resource state 权威的组合方式。

这些问题未确认前不得在 Game/Render 第一批中增加长期正式 RHI 策略。

## 17. 错误、初始化与 Shutdown

### 17.1 Renderer terminal failure

`DeviceLost`、状态未知的 submit/presentation failure 等不可恢复错误进入 process-wide renderer terminal state。检测到 terminal 后：

```text
latch terminal renderer state
stop accepting new RenderCommand
Task Graph 继续 FIFO 取出 pending RenderCommandTask
RenderCommandTask 跳过 callable，只在 Rendering Thread 析构 payload
FrameEndFence/waiter 被唤醒并观察 terminal result
进入专用 Rendering Thread teardown
```

Task Graph 不新增 Render 专用定向取消 API，也不在任意取消线程销毁 Rendering Thread payload。程序 contract 误用与 backend
terminal failure 必须区分：前者直接 assert/crash，后者保留原始 RHI 诊断并结束渲染生命周期。

### 17.2 引擎初始化阶段

Render Thread 的存在是引擎初始化阶段保证，不是普通 enqueue 的可恢复条件：

```text
create TaskGraph and attach GameThread
create/attach RenderingThread
initialize Renderer/RHI and minimal RHIDeviceCommandList
enable the single process-wide RenderCommand façade
create World/Scene/RenderViewport/render resources
enter normal tick
```

初始化失败时不开放正常 RenderCommand 生产；在已完成初始化的线程域内销毁部分状态并返回原始错误。single-thread 模式在
Game Thread 执行相同初始化顺序，然后启用立即执行 façade。

### 17.3 Shutdown

```text
stop Game tick and new draw requests
World/Component unregister and enqueue remove/release
close the normal RenderCommand producer entrance
enqueue/wait the final RenderCommandFence
destroy Scene/resources/viewports/Renderer/RHI on Rendering Thread
request RenderingThread return and join
shutdown TaskGraph explicitly
```

正常 shutdown 必须 drain 并执行全部已接受命令，不设置 terminal、不跳过 remove/release。关闭入口后继续 enqueue、销毁 Scene 时
仍存在注册 Proxy、或 Task Graph 早于 RenderingThread shutdown 都属于 lifecycle contract violation。正常 drain、初始化中途失败、
terminal renderer error、窗口提前关闭和 pending RHI work 都必须有独立测试。

## 18. 本轮评审结果

### 18.1 RenderCommand 与帧同步

- RC-001 已确认：模板 façade 直接存储 move-only callable；签名为 `void(RHIDeviceCommandList&) noexcept`，无逐命令 result；
- RC-002 已确认：SceneInterface add/update/remove 为 fire-and-forget，无逐操作异步回执；
- RC-003 已确认：Component 保留 non-owning `SceneProxy*`，Scene/SceneInfo RAII 独占拥有 Proxy；
- RC-004 已确认：Game setter 立即 enqueue，Rendering Thread 合并昂贵 Scene work；
- RC-005 已确认：普通 ViewFamily 无独立 completion；
- RC-006 已确认：terminal 后 task 跳过 pending callable，正常 shutdown 仍 drain；
- RC-007 已确认：双 GraphEvent fence；lag 开启时首帧不等待，从第二帧等待上一 fence；
- RC-008 已确认：Game Thread 单 producer 由运行时检查强制，误用 fail fast。

### 18.2 RHI device command list

- RCL-001 已确认：采用最小 `RHIDeviceCommandList` 结构；
- RCL-002 已确认：保留 `RHICommandList` 名称；
- RCL-003 已确认：最小结构位于 public RHI；
- RCL-005 已确认：所有 RenderCommand 统一接收 `RHIDeviceCommandList&`；
- RCL-006 已确认：不新增通用 GPU Ready 状态；RHI 引用发布与顺序 contract 后续细化；
- RCL-004、RCL-007 至 RCL-010 后置到 RHI 专项设计，本轮不固化答案。
