# game-render-framework/task-graph-runtime Specification

## Purpose
定义 Game/Render 框架依赖的进程级 Task Graph active instance、受控active access、Named Thread attach/route/pump、completion和显式shutdown行为，使所有框架任务共享同一个可诊断调度域，同时不把Task Graph变成Renderer或RHI service locator。

## Requirements

### Requirement: 单一 active instance
进程 MUST同时最多存在一个active或starting Task Graph instance。factory MUST在创建workers前保留active creation slot；config validation或worker startup失败 MUST释放reservation，全部初始化成功后才能发布active pointer。实例由Engine显式创建和shutdown，不得替换已有instance或延迟到首次`get()`时隐式构造。

`shutdown()`开始停止新Task acceptance时 MUST撤销active publication，使新的`is_running()`返回false且`get()`无法取得引用；已经持有的non-owning引用只能在composition root保证的shutdown边界内完成既有drain，不得保存到instance销毁后。

#### Scenario: 重复启动
- **WHEN** active instance 存在时再次启动 Task Graph
- **THEN** 系统 MUST 返回可诊断错误且不得替换现有 instance

#### Scenario: 并发 creation
- **WHEN** 一个Task Graph仍在创建workers而另一个调用同时请求create
- **THEN** 第二个create MUST因starting reservation失败，不能创建第二组workers或抢先发布active pointer

#### Scenario: Worker startup失败
- **WHEN** factory已保留creation slot但某个worker创建失败
- **THEN** factory MUST回收已创建workers、释放reservation并返回原始错误，使后续一次全新create仍有明确机会

#### Scenario: shutdown 后访问
- **WHEN** Task Graph 已 shutdown
- **THEN** `is_running()` MUST 为 false，访问 MUST fail fast 或返回明确 stopped 状态，不得自动重建

### Requirement: Active access 不是业务 service locator
`TaskGraphInterface::get()` MUST只返回当前active scheduler的non-owning reference。它 MAY供Core GraphTask/GraphEvent helper、RenderCommand内部transport publication和其他生命周期明确受composition root控制的基础设施入口使用；不得通过它取得Renderer、RenderScene、RenderResourceManager、RHI或其他业务service，也不得让业务模块保存该引用越过Task Graph shutdown。

`is_running()`只表达active publication是否存在，不保证特定NamedThread已经attach、Renderer已经Running或普通RenderCommand admission已经开放。调用方不得用它替代RenderingThread/Renderer ready handshake。

#### Scenario: 无 active instance调用get
- **WHEN** startup尚未发布active instance或shutdown已经撤销publication
- **THEN** `get()` MUST抛出/报告明确Stopped或InvalidState诊断，不得创建instance或返回空悬引用

#### Scenario: Task Graph active但Renderer未ready
- **WHEN** Task Graph已经running而RenderingThread/Renderer仍在Starting
- **THEN** `is_running()` MAY为true，但普通Game/Scene/resource producer MUST仍等待Renderer ready和RenderCommand publication

#### Scenario: 业务模块需要渲染操作
- **WHEN** Component或Material需要投递render update
- **THEN** 它 MUST使用Scene/resource/RenderCommand窄入口，不得通过TaskGraphInterface::get()查找Renderer或绕过领域contract

### Requirement: Task Graph 所有权不越过 Core 边界
Engine MUST独占`TaskGraphInterface`；Task Graph MUST独占其AnyWorker worker threads和GraphTask storage，但不拥有GameThread、OS RenderingThread或Renderer domain。GameThread由Engine调用attach；OS RenderingThread由`RenderingThread` controller创建/join并在目标thread上attach。GraphTask拥有TaskType payload，GraphEventRef只共享completion state而不保留Renderer service ownership。

#### Scenario: RenderingThread生命周期
- **WHEN** MultiThread RenderingThread创建OS thread
- **THEN** 该OS thread MUST attach到Task Graph并pump named queue，但Task Graph不得负责销毁Renderer或join由RenderingThread controller拥有的thread

#### Scenario: Task payload完成
- **WHEN** GraphTask执行、失败或cancel完成
- **THEN** Task Graph MUST按对应contract exactly-once销毁TaskType payload并发布tracked completion，不得把payload ownership返还给业务调用方

### Requirement: Named Thread transport 可用性
GameThread与logical RenderingThread MUST在普通RenderCommand开放前完成所需attach。MultiThread模式下Engine先attach GameThread，OS RenderingThread再attach`NamedThread::RenderingThread`；SingleThread模式只attachGameThread，`get_render_thread()` MUST返回GameThread，不得额外attach或伪造独立RenderingThread binding。

一个NamedThread同一时间只能绑定一个owner OS thread，一个OS thread也不得重复attach到同一或不同active Task Graph。Task desired thread在构造完成后固定，执行时不得漂移到错误thread。

#### Scenario: RenderingThread 尚未 attach
- **WHEN** 调用方尝试向 RenderingThread named queue 投递正式命令
- **THEN** 框架 MUST 拒绝投递并记录生命周期诊断

#### Scenario: SingleThread mapping
- **WHEN** Task Graph config为single-thread
- **THEN** desired RenderingThread task MUST确定映射到已attach的GameThread FIFO，AnyWorker fallback也由GT pump，但两者仍保留各自预算/任务语义

#### Scenario: 重复 attach
- **WHEN** NamedThread已经有owner或当前OS thread已attach
- **THEN** attach MUST返回InvalidState/InvalidCaller类诊断，不得替换binding或允许两个consumer pump同一named queue

### Requirement: Named queue 保证领域顺序但不复制调度系统
GameThread与RenderingThread MUST使用Task Graph既有named MPSC queue；同一producer到同一named queue MUST保持FIFO。RenderingThread named task不占AnyWorker fixed outstanding budget，也不得因任意固定数量阈值被丢弃；动态allocation failure属于framework fatal，RenderCommand无返回值façade必须fail fast而不能正常返回。

AnyWorker的bounded budget、`Overloaded`和priority只适用于worker任务，不得用于普通RenderCommand backpressure。RenderScene、Material、Resource和Draw不得建立第二套正式queue。

#### Scenario: GT连续投递render work
- **WHEN** GT依次投递Scene update、Material update、Draw与tracked Fence
- **THEN** RenderingThread named queue MUST按接受顺序执行或terminal-dispose，不得由Task Graph按领域类型重排

#### Scenario: AnyWorker饱和
- **WHEN** AnyWorker outstanding budget已满但RenderingThread named queue仍可分配节点
- **THEN** worker task MAY返回Overloaded，普通RenderCommand不得因为该worker budget被拒绝或同步执行

### Requirement: Named Thread pump、wake与return明确
只有NamedThread owner可pump对应queue。MultiThread RenderingThread MUST只pumpRenderingThread named queue；空闲时必须通过Task Graph wait机制休眠。新task、completion相关wake或`request_return()` MUST唤醒owner，不能busy spin或依赖额外render queue sentinel。

`request_return(RenderingThread)`只停止named queue pump，不执行Renderer teardown、不清理RenderScene且不joinOS thread；composition root MUST在Renderer teardown和ownership disposal完成后才request return，随后由RenderingThread controller join。

#### Scenario: 错误thread pump
- **WHEN** 非owner thread尝试process RenderingThread named queue
- **THEN** Task Graph MUST诊断InvalidCaller且不得在该thread执行render task

#### Scenario: 空闲pump收到return
- **WHEN** RenderingThread named queue为空且owner正在wait
- **THEN** request_return MUST唤醒owner并让pump有界返回

#### Scenario: Return后投递
- **WHEN** RenderingThread pump已经return且transport producer-stop边界已关闭
- **THEN** 新正式RenderCommand属于lifecycle contract violation，不得留在无人消费的named queue

### Requirement: FireAndForget 与 tracked completion 都不丢 payload
普通RenderCommand SHALL使用`SubsequentsMode::FireAndForget`，RenderCommandFence SHALL使用`TrackSubsequents`。所有已接受tracked task MUST exactly-once发布Succeeded、Failed或Cancelled completion；FireAndForget没有对外GraphEvent，但其TaskType/callable payload仍 MUST exactly-once执行或处置并析构。

TaskType exception MUST在executor边界转换为Failed outcome，不得跨线程传播。普通RenderCommand额外由其capability约束为`void() noexcept`，避免把Renderer业务异常交给通用Task Graph。

#### Scenario: Terminal pending FireAndForget
- **WHEN** Renderer terminal使普通RenderCommand业务体skip
- **THEN** Task Graph/transport MUST仍让callable storage按FIFO到达logical RT disposal并exactly-once析构

#### Scenario: Fence task failure
- **WHEN** tracked Fence task遇到framework execution failure
- **THEN** completion MUST关闭为Failed并唤醒waiter，不能因失败永久保持Pending

### Requirement: Wait helping 遵守线程归属
GameThread wait MAY pump其Game named queue并有限帮助AnyWorker；RenderingThread wait MAY pumpRender named queue但默认不执行任意重worker任务；worker wait MAY帮助其他ready worker task；Unknown/external thread只能阻塞在completion event。SingleThread下GT MUST pump所有映射到自己的task以取得进展。

self-wait、依赖环或唯一NamedThread owner等待只能由自己执行的不可达task MUST产生DeadlockRisk类诊断。timeout只结束wait，不取消task或释放仍被running task使用的payload。

#### Scenario: GT等待RenderCommandFence
- **WHEN** MultiThread GT等待RenderingThread tracked completion
- **THEN** GT不得在自身执行RenderingThread task，只能等待OS RenderingThread取得进展，并 MAY有限帮助独立AnyWorker work

#### Scenario: SingleThread wait
- **WHEN** GameThread也是logical RT且等待映射到自身queue的completion
- **THEN** wait MUST pump该queue；若形成self-wait或递归无法进展，Task Graph MUST诊断DeadlockRisk

### Requirement: 显式 shutdown
Task Graph MUST在RenderingThread join后由Engine显式shutdown。正常流程 MUST先停止普通producer、完成RenderCommand/Fence drain、teardown Renderer、request RenderingThread return并join，再以`TaskGraphShutdownMode::Drain`关闭graph和workers。`Drain`开始前不得留下无人能pump的RenderingThread task。

`CancelPending`只用于create/start failure或无法正常drain的graph级回滚；running task不得强杀。Renderer/Proxy/Material/Texture/SceneRenderer等要求logical RT disposal的payload MUST在RenderingThread join前由render terminal contract处置，绝不能遗留给join后的generic CancelPending在GT析构。

shutdown MUST停止新Task acceptance、撤销active publication、唤醒worker/named/external waiters，并保证所有已接受tracked task exactly-once发布completion outcome。Task Graph析构不得猜测正常策略；Engine必须显式调用并检查shutdown result。

#### Scenario: Engine 正常关闭
- **WHEN** RenderingThread 已完成 teardown 并 join
- **THEN** Engine MUST shutdown Task Graph，唤醒 waiter，并在销毁前验证无 active named thread

#### Scenario: Drain前仍有Render task
- **WHEN** RenderingThread已join但RenderingThread named queue仍有accepted task
- **THEN** 这属于composition shutdown violation；Task Graph不得假装Drain成功或在GT执行具有RT-only ownership的payload

#### Scenario: Create failure rollback
- **WHEN** Engine/RenderingThread startup失败且没有进入正常frame producer阶段
- **THEN** Engine MAY使用CancelPending回收通用graph task，但必须先保证任何已创建Renderer-domain ownership已在logical RT回滚

#### Scenario: 析构前未shutdown
- **WHEN** Engine销毁TaskGraphInterface但没有显式shutdown
- **THEN** 实现 MUST产生lifecycle diagnostic；析构安全兜底不得成为吞掉shutdown error的常规路径
