# Toy3d Game/Render 多线程框架设计

## 1. 文档状态与职责

本文是 Toy3d 新一代 Game/Render 多线程框架的唯一总体设计基线。后续实现、评审和专项设计均以本文为起点；旧的 frame packet、
私有 render queue、dispatcher、completion 以及与本文冲突的 Game/Render Thread 设计不再构成兼容约束。

本文只定义长期稳定的框架：线程角色、核心对象、命令流、所有权、生命周期、同步、错误模型、模块边界和实施顺序。SceneProxy、
RenderResource、ViewFamily、viewport、RHI device command 的具体数据结构与算法在框架落地后逐模块设计，不提前塞回本文。

当前可直接复用的基础是 `Toy3dTaskGraph`：它已经提供 `GameThread`、`RenderingThread`、`AnyWorker` named thread，支持外部线程
attach、named queue pump、GraphTask、GraphEvent、wait、request return 和 single-thread logical render-thread 映射。

当前代码中的最小 `RHIDeviceCommandList` 可以保留。`RenderCommandBinding` 是框架定型前产生的原型，不属于本文确认的公共概念，
正式实现时删除并重写相关测试，不为它保留兼容层。

## 2. 目标与非目标

### 2.1 目标

1. 建立长期可用的 Game Thread 到 Rendering Thread 单向命令流。
2. 总体职责、命令投递、线程启动、flush 和 fence 语义尽可能贴近 UE4.27，降低后续架构沟通成本。
3. 直接复用 Task Graph 的 `RenderingThread` named queue，不建立第二条私有 queue 或 dispatcher。
4. multi-thread 与 single-thread 使用相同的 RenderCommand callable、Renderer 和 RHI 入口。
5. Game 对象与 Rendering Thread 状态彻底分离，跨线程数据使用 owned value、稳定 identity 或所有权转移。
6. 明确初始化、正常 drain、terminal renderer failure 和 shutdown 的顺序，所有已接受任务都得到确定处理。
7. 先建立最小 `RHIDeviceCommandList` 框架边界，后续再设计资源命令、录制、dispatch 和提交细节。
8. 为 Scene bridge、RenderResource、ViewFamily、pass 级并行录制和未来 RDG 提供稳定扩展点。

### 2.2 非目标

第一阶段不实现：

- RHI Thread、UE 的软件 RHI command chain、bypass 或并行翻译系统；
- async compute、多 graphics queue、GPU scene、RDG、pass 内并行或 draw-batch 并行；
- 完整 `RHIDeviceCommandList` 资源 API；
- SceneProxy、Material、Mesh、ViewFamily 或 viewport 的最终业务模型；
- editor screenshot、readback 等专用异步结果通道；
- 对旧 `RenderFramePacket/Dispatcher/Queue/Completion` 的兼容 adapter。

## 3. UE4.27 参考与 Toy3d 选择

### 3.1 UE4.27 参考事实

UE4.27 的核心调用链是：

```text
ENQUEUE_RENDER_COMMAND
    → EnqueueUniqueRenderCommand()
    → TEnqueueUniqueRenderCommandType<Lambda>
    → Task Graph RenderThread named queue
    → Lambda(FRHICommandListImmediate&)
```

- `FRenderCommand` 指定 RenderThread 和 `FireAndForget`。
- `TEnqueueUniqueRenderCommandType` 直接保存具体 lambda 类型，支持 move capture，不经过 `std::function`。
- 已在 Rendering Thread 时立即执行；关闭 threaded rendering 时在 Game Thread 立即执行同一 callable。
- `StartRenderingThread()` 等待 Rendering Thread 完成 Task Graph binding 后才允许投递任务。
- `StopRenderingThread()` 先 flush 已投递命令，再发送退出任务并 join。
- `FRenderCommandFence` 使用 Task Graph completion event 表示 Rendering Thread CPU 到达点。
- RenderCommand 通过 `GetImmediateCommandList_ForRenderCommand()` 取得当前 immediate RHI command list。

### 3.2 Toy3d 明确取舍

| 主题 | Toy3d 选择 |
|---|---|
| Task Graph | 使用 composition root 注入的 `TaskGraphInterface`，不复制 UE 的全局 Task Graph singleton。 |
| 命令 façade | 提供 UE 风格 `enqueue_render_command()` 模板函数，不引入 UE 宏和类型前缀。 |
| 命令 payload | GraphTask 直接保存具体 move-only callable，不使用 `std::function` 或公共 type-erased command。 |
| RHI 参数 | 使用最小 `RHIDeviceCommandList&`，不把它命名为 immediate，也不预设 RHI Thread。 |
| Rendering Thread | 独占 Task Graph named queue 的 pump；不拥有 Scene、pass 调度或后端策略。 |
| producer | 第一阶段正式支持 Game Thread 投递；Rendering Thread 内部调用可立即执行；其他线程 fail fast。 |
| single-thread | `TaskGraphInterface::get_render_thread()` 映射到 `GameThread`，直接执行同一 callable。 |
| façade 注册 | 作为 `RenderingThread` 启停的内部实现，不公开 `RenderCommandBinding`、registry 或第二个 dispatcher。 |
| terminal | Renderer 保存原始 terminal 结果；pending RenderCommand 继续按 FIFO 取出但跳过 callable。 |

采用 UE 术语表示职责，不复制 UE 的全局对象体系、宏系统、`F`/`E`/`T`/`I` 前缀或历史兼容层。

## 4. 核心对象与稳定职责

| 对象 | 稳定职责 | 明确不负责 |
|---|---|---|
| composition root | 创建 Task Graph、Renderer、RenderingThread；按顺序启动和关闭框架 | Scene 更新、RHI 命令翻译 |
| `TaskGraphInterface` | named thread routing、FIFO queue、GraphTask、GraphEvent、wait、return request | Renderer 生命周期、Scene/RHI 策略 |
| `RenderingThread` | attach `RenderingThread`、初始化握手、pump named queue、request return、join、single-thread lifecycle | Scene 状态、渲染 pass、RHI backend 策略 |
| `Renderer` | Rendering Thread 渲染域、terminal state、RHI/device 生命周期、持有 `RHIDeviceCommandList` | Game 对象生命周期、Task Graph queue 实现 |
| `RenderCommandTask<Callable>` | 保存一次具体 callable 并路由到 logical Rendering Thread | GPU completion、业务级返回值 |
| `enqueue_render_command()` | 校验生命周期和 producer，选择 enqueue 或 inline 路径 | 拥有 queue、启动线程、恢复 renderer |
| `RenderCommandFence` | 标记此前 RenderCommand 的 Rendering Thread CPU 到达点 | GPU fence、present completion |
| `RHIDeviceCommandList` | RenderCommand 的稳定 device-level RHI 参数和未来扩展入口 | Task Graph、Scene、ViewFamily、Renderer shutdown |

不新增 `RenderCommandBinding`、`RenderCommandDispatcher` 或 `RenderCommandQueue`。Task Graph 已经是唯一 transport；Renderer 与
RenderingThread 已经分别表达渲染状态和执行线程，再增加公开的 binding/dispatcher 只会重复生命周期语义。

## 5. 总体拓扑

### 5.1 Multi-thread 模式

```text
Game Thread
    │
    │ enqueue_render_command(Name, move-only callable)
    ▼
RenderCommandTask<Callable>
    │
    │ TaskGraph RenderingThread named queue（FIFO）
    ▼
RenderingThread
    │
    │ callable(Renderer::device_command_list())
    ▼
Renderer / RenderScene / public RHI
    │
    ▼
RHI context → RHI command list → queue submit → GPU
```

Game → Render 的顺序由 Task Graph named queue 保证；RHI 录制顺序由 command context 保证；GPU 提交顺序由 RHI queue 保证。
三层顺序不能用同一个 frame id、GraphEvent 或 GPU completion value 混合表达。

### 5.2 Single-thread 模式

```text
Game Thread = logical Rendering Thread
    │
    │ enqueue_render_command()
    ▼
立即执行相同 callable(RHIDeviceCommandList&)
    ▼
相同 Renderer / RenderScene / public RHI 路径
```

single-thread 只改变调度方式，不允许 Gameplay 绕过 RenderCommand 直接修改 RenderScene 或调用 RHI，也不建立稍后 pump 的同线程
临时队列。

## 6. 所有权与线程归属

| 对象 | CPU 所有者 | 创建/初始化线程 | 可变状态线程 | 销毁/teardown 线程 |
|---|---|---|---|---|
| `TaskGraphInterface` | composition root | Game Thread | 内部同步 | Game Thread，且晚于 RenderingThread join |
| `RenderingThread` | composition root | Game Thread | Game Thread 控制生命周期；内部线程只执行 run loop | Game Thread join 后 |
| `Renderer` 对象 | composition root | Game Thread 创建稳定外壳 | Rendering Thread 初始化并独占渲染状态 | Rendering Thread 先 teardown，Game Thread 后销毁空外壳 |
| `RHIDeviceCommandList` | `Renderer` | logical Rendering Thread | logical Rendering Thread | logical Rendering Thread |
| `RenderCommandTask` | Task Graph | producer 构造 | logical Rendering Thread 执行 | logical Rendering Thread 执行或丢弃后 |
| `RenderCommandFence` event | Game Thread 发起者共享 | Game Thread | Task Graph completion | waiter 释放最后引用时 |

Renderer 对象地址在 façade 开放期间保持稳定。Game Thread 只观察 Renderer 暴露的原子 lifecycle/terminal 结果，不读取 Scene、RHI
资源或其他 Rendering Thread 可变状态。

RenderCommand payload 只允许：

- owned value snapshot；
- stable typed ID；
- `unique_ptr` 所有权转移；
- 明确 immutable 且生命周期覆盖命令执行的共享版本；
- 由 drain contract 保证生命周期的稳定 Scene、Proxy、Viewport identity。

禁止捕获可被 Game Thread 继续修改的对象引用，以及可在 Rendering Thread 解引用的 World、Actor、Component、Camera 等 Game
对象指针。

## 7. RenderingThread 设计

### 7.1 Multi-thread 启动

composition root 在 Game Thread 执行：

```text
1. create TaskGraph
2. attach GameThread
3. create stable Renderer shell
4. create and start RenderingThread
5. RenderingThread attach NamedThread::RenderingThread
6. RenderingThread initialize Renderer/RHI/RHIDeviceCommandList
7. publish startup result and signal ready
8. enable process-wide enqueue_render_command() façade
9. enter normal Game loop
```

第 8 步之前投递普通 RenderCommand 是 lifecycle contract violation。Task Graph 会拒绝尚未 attach 的 named target，因此 façade 绝不能
早于 RenderingThread ready handshake 开放。

RenderingThread run loop只调用：

```cpp
task_graph.process_thread_until_request_return(NamedThread::RenderingThread);
```

它不维护另一条 condition-variable queue，也不主动扫描 World、Scene 或资源 dirty list。

### 7.2 Single-thread 启动

```text
1. create TaskGraph with multithreaded=false
2. attach GameThread
3. create stable Renderer shell
4. initialize Renderer/RHI/RHIDeviceCommandList on GameThread
5. enable enqueue_render_command() façade
6. enter normal Game loop
```

single-thread 模式不创建 OS Rendering Thread，但仍创建同一个框架级 `RenderingThread` lifecycle controller，使初始化、façade 启停、
Renderer teardown 和测试入口保持一致。

## 8. RenderCommand 设计

### 8.1 公共调用形状

```cpp
enqueue_render_command(
    "CommandName",
    [payload = std::move(payload)](
        RHIDeviceCommandList& device_command_list) noexcept
    {
        // Rendering Thread work.
    });
```

callable contract 固定为：

```text
void(RHIDeviceCommandList&) noexcept
```

- callable 必须以具体类型直接存储并支持 move-only capture；
- 普通命令不返回逐命令 result，不公开 completion future；
- 命令名用于诊断、profiling 和未来 trace，不决定类型身份；
- callable 抛异常、错误线程调用、未启动或 shutdown 后调用均为 contract violation 并 fail fast；
- 内存耗尽或 Running 状态下 Task Graph 拒绝 named task 属于 fatal framework failure，不能静默丢命令。

### 8.2 GraphTask 语义

`RenderCommandTask<Callable>` 是 `GraphTask` 的专用 payload：

```text
desired thread   = NamedThread::RenderingThread
priority         = Normal
subsequents mode = FireAndForget
```

普通 RenderCommand 不创建可等待 completion。需要同步时使用独立的 `RenderCommandFence` task，避免每条命令承担 GraphEvent 成本。

### 8.3 执行选择

```text
Game Thread + multi-thread  → enqueue RenderingThread GraphTask
Rendering Thread            → inline execute
Game Thread + single-thread → inline execute
其他线程                     → 第一阶段 fail fast
```

无论 enqueue 还是 inline，callable 都通过同一个内部执行入口获得 Renderer 当前的 `RHIDeviceCommandList&` 并检查 terminal state。

### 8.4 Process-wide façade

进程第一阶段只允许一个 active Renderer。为了提供 UE 风格 free-function façade，RenderCore 内部必须保存当前 Task Graph、Renderer 和
façade lifecycle 的最小非 owning 引用。这是 process-wide façade 的实现需要，必须满足：

- 只由 `RenderingThread` start/stop lifecycle 修改；
- façade ready 前为空，shutdown 完成后清空；
- 不暴露可构造的 `Binding`、registry、singleton 或 service locator 类型；
- 不拥有 Task Graph、Renderer、queue 或 `RHIDeviceCommandList`；
- 初始化和清理有严格顺序，不能静态隐式初始化；
- 以后如支持多个独立 Renderer，先修改本文的 process model，不能把单例假设扩散到业务模块。

业务调用方只包含和调用 `enqueue_render_command()`，不能取得或替换内部 façade 状态。

这是对“library core 不保存可变全局状态”规则的一个显式、受限例外：UE 风格 free-function façade 和当前单 active Renderer process
model 需要一个进程级路由点。例外只允许保存由 composition root 注入的 non-owning 引用和 lifecycle 标志，不允许拥有服务或隐藏创建
依赖。未来若引入多 Renderer process model，收敛路径是先把 façade 改为显式 renderer domain 参数，再移除这个单路由点。

## 9. RHIDeviceCommandList 最小边界

`RHIDeviceCommandList` 是 RenderCommand 到 public RHI 的 device-level 命令入口。框架阶段只确认：

1. 类型位于 public RHI，不包含 Vulkan、D3D11、D3D12 或平台原生类型；
2. Renderer 创建、持有并在 logical Rendering Thread 销毁；
3. 所有 RenderCommand 使用同一个参数类型；
4. 它不是 `RHIGraphicsCommandContext`、GPU command buffer 或 Task Graph command；
5. 它不理解 Scene、ViewFamily、Viewport、Task Graph 或 RenderingThread shutdown；
6. 第一阶段可以没有业务方法，后续逐项增加 device-level resource work；
7. 不预设 RHI Thread、软件 replay chain、batch 阈值、自动 submit、release 或 staging 策略。

后续专项必须分别定义资源 init/update/release、录制、finish、dispatch、提交失败、completion 和 deferred deletion。任何 backend 都不得
把未实现能力伪装为成功。

Vulkan、D3D11 FL11_0、D3D12 和移动端 `VulkanPortable v1` 都能实现该空框架边界；差异只会在后续具体 RHI 命令语义中出现。

## 10. RenderCommandFence 与帧同步

`RenderCommandFence` 使用一个目标为 logical Rendering Thread、`TrackSubsequents` 的 GraphTask completion event：

```text
Game Thread commands A, B, C
    → enqueue fence task
    → wait fence GraphEvent
```

fence 完成表示 A、B、C 的 Rendering Thread CPU callable 已经执行。框架阶段不承诺 GPU 已执行、present 已显示或 deferred deletion 已
回收。

当 `RHIDeviceCommandList` 后续具备 pending device work 时，fence task 在完成前必须执行文档另行确认的最小 dispatch 边界；即使如此，
它仍然不是 GPU completion。GPU 生命周期继续由 `RHIQueueCompletionValue`、frame slot 或显式 `RHIGPUFence` 表达。

普通每帧同步由两个 fence 轮转：

```text
one-frame lag enabled  → frame N 等待 frame N-1 fence
one-frame lag disabled → frame N 等待 frame N fence
```

lag 策略属于 Game loop/FrameEndSync，不改变 RenderCommand queue、Renderer 或 RHI 语义。

## 11. 生命周期状态机

框架生命周期为：

```text
Stopped
   │ start
   ▼
Starting ──failure──→ Stopped
   │ ready
   ▼
Running ──normal shutdown──→ Draining ──→ Stopping ──→ Stopped
   │
   └──renderer fatal──→ TerminalDraining ──→ Stopping ──→ Stopped
```

### 11.1 正常 shutdown

Game Thread 顺序固定为：

```text
1. stop Game tick and new draw production
2. enqueue remaining Scene remove and resource release commands
3. enqueue final RenderCommandFence
4. close the public RenderCommand façade
5. wait final fence
6. dispatch dedicated Renderer teardown task through the lifecycle path
7. wait Renderer/RHI teardown
8. request RenderingThread return
9. join RenderingThread
10. shutdown TaskGraph
11. destroy Renderer shell and framework objects
```

正常 shutdown 必须执行全部已接受 callable。关闭 façade 之后的普通 enqueue 是 contract violation。Renderer teardown task 不是普通业务
RenderCommand，使用内部 lifecycle path，避免为了关闭系统重新开放公共入口。

### 11.2 Terminal renderer failure

`DeviceLost`、状态未知的 submit/present failure 或关键同步损坏进入 Renderer terminal state：

```text
1. Renderer latch original terminal result
2. wake Game Thread/frame waiters
3. pending RenderCommandTask 继续由 Task Graph FIFO 取出
4. task 检查 terminal 后跳过 callable
5. callable payload 在 logical Rendering Thread 析构
6. Game Thread 停止生产并关闭 façade
7. accepted fences 完成并向 waiter 暴露 terminal result
8. 进入专用 Renderer teardown 和 RenderingThread shutdown
```

Renderer terminal 与 façade closed 是两个不同状态。异步 terminal 与 Game Thread 停止生产之间可能存在短暂竞态；这一窗口内已经被
Task Graph 接受的命令必须按上述规则在 Rendering Thread 丢弃，不能在任意线程取消并析构 payload。

## 12. 错误模型

| 类别 | 示例 | 行为 |
|---|---|---|
| contract violation | 未启动 enqueue、关闭后 enqueue、错误 producer、错误 callable 签名 | 记录诊断并 fail fast |
| startup failure | thread create、attach、Renderer/RHI initialize 失败 | 不开放 façade，返回原始结构化错误并清理部分状态 |
| framework fatal | Running 时 Task Graph 拒绝 named task、accepted fence 永久不能完成、OOM | 锁存 fatal、唤醒 waiter、终止正常渲染生命周期 |
| recoverable frame result | viewport `NotReady/OutOfDate/Suboptimal` | 由 Renderer/viewport 专项处理，不停止 RenderingThread |
| terminal renderer failure | `DeviceLost/BackendFailure`、状态未知同步失败 | 保留原始错误，pending callable 跳过，进入 terminal teardown |

RenderCommand callable 不用异常或普通逐命令 completion 返回失败。需要结果的 screenshot、readback、asset cook 等操作使用各自专用
协议，不污染普通命令流。

## 13. 模块与依赖方向

建议长期落点：

```text
engine/runtime/rendercore/
├── render_command.*
├── render_command_fence.*
└── frame_end_sync.*

engine/runtime/renderscene/
├── rendering_thread.*
├── renderer.*
├── scene.*
└── scene_renderer.*

engine/runtime/drivers/rhi/
└── rhi_device_command_list.*
```

依赖方向：

```text
GameScene → RenderCore façade/contracts
RenderScene → RenderCore + public RHI
RenderingThread → TaskGraph + Renderer lifecycle interface
Renderer → RenderScene + public RHI
RenderCore → TaskGraph + public RHIDeviceCommandList declaration
public RHI → Core，不依赖 GameScene/RenderScene/RenderingThread
backend → public RHI
```

上层不得包含 Vulkan/D3D 类型；public RHI 不得包含 Task Graph、Scene 或 Renderer 类型；RenderingThread 不得成为万能服务容器。

## 14. 分批实施顺序

### 批次 1：线程与命令主干

1. 保留最小 `RHIDeviceCommandList`；
2. 删除 `RenderCommandBinding` 原型和对应生命周期假设；
3. 建立最小 `Renderer` shell，持有 `RHIDeviceCommandList` 和 terminal state；
4. 实现 `RenderingThread` multi-thread/single-thread lifecycle；
5. 实现内部 façade enable/disable 与 `RenderCommandTask<Callable>`；
6. 验证 ready handshake、FIFO、move-only、inline 和错误线程检查。

### 批次 2：同步与退出

1. 实现 `RenderCommandFence`；
2. 实现正常 drain、Renderer teardown task、request return 和 join；
3. 实现 terminal latch、pending callable skip 和 waiter wakeup；
4. 实现双 fence `FrameEndSync` 与 one-frame lag policy；
5. 完成初始化失败和 shutdown 测试矩阵。

### 批次 3：Scene bridge

1. 设计 `SceneInterface`、typed SceneProxy 和 SceneInfo ownership；
2. Component register/update/unregister 生成 RenderCommand；
3. Rendering Thread 增量维护 Scene，不扫描 World；
4. 删除旧 snapshot scene batch 路径。

### 批次 4：RenderResource 命令

1. 专项设计 `RHIDeviceCommandList` 的资源 init/update/release 与 dispatch；
2. 接入 upload、replacement、release 和 GPU in-flight 生命周期；
3. 验证 upload-before-draw 和 deferred deletion；
4. 删除旧 resource batch/collector 路径。

### 批次 5：ViewFamily 与正式渲染入口

1. 设计 command-owned `SceneViewFamily` value snapshot；
2. 每个观察请求生成独立 draw RenderCommand；
3. 接入 viewport frame、SceneRenderer、record、submit 和 present；
4. 删除剩余 `RenderFramePacket/Dispatcher/Queue/Completion` 正式入口。

每个批次必须形成可构建、可测试的单一路径。允许在一个开发分支内大规模删除旧代码，但不新增长期 adapter，也不让新旧正式入口
共同承担生产职责。

## 15. 框架验收矩阵

### 15.1 必须通过

- RenderingThread attach 完成前 façade 不可用，ready 后 Task Graph 不返回 `TargetUnavailable`；
- multi-thread 命令只在 Rendering Thread 执行并保持 FIFO；
- single-thread 使用同一 callable 和 Renderer 路径并立即执行；
- move-only payload 只移动一次，正常执行后在 logical Rendering Thread 析构；
- terminal 后 pending callable 不执行，payload 仍在 logical Rendering Thread 析构；
- 普通命令为 `FireAndForget`，fence 使用独立 GraphEvent；
- fence 只表达 Rendering Thread CPU 到达，不冒充 GPU completion；
- 正常 shutdown 执行全部已接受命令并按 Renderer → RenderingThread → TaskGraph 顺序退出；
- 初始化失败不开放 façade且无遗留线程；
- 错误线程、未启动和关闭后 enqueue 可诊断并 fail fast；
- public API 中不存在 `RenderCommandBinding`、私有 render queue、backend 类型或可变全局 RHI 指针。

### 15.2 后续扩展不得破坏

- 一个 Game frame 可产生零到多个 draw request；
- 每个完整 pass 可获得独立 RHI recording context 并串行退化；
- 增加 D3D11、D3D12 或移动端 Vulkan 不改变 Game/Render Thread contract；
- 引入 RHI Thread、RDG 或多 queue 前必须另行设计，不能改变已有 RenderCommand payload ownership；
- editor、tools 或 headless 模式可选择 single-thread，但不能建立 Gameplay → RHI 旁路。

## 16. 后置专项入口

框架实现完成后，按以下顺序分别设计，不在本文提前固化：

1. SceneInterface、SceneProxy、SceneInfo 与 dirty merge；
2. `RHIDeviceCommandList` 资源命令、dispatch、错误与 completion；
3. RenderResource upload、replacement、release 和 deferred deletion；
4. SceneViewFamily、RenderViewport 与 SceneRenderer；
5. pass 级并行录制；
6. 正式 RDG、compute 和 async compute。

后置不等于允许临时正式接口。任何专项开始前都必须先定义用例、所有权、线程、错误、三后端映射和删除旧路径的条件。
