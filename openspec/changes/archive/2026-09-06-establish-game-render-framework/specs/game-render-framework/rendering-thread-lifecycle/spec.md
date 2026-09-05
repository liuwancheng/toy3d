## Purpose

定义独立 Rendering Thread 与 single-thread logical fallback 的启动握手、Named Thread pump、返回和 join，使 Renderer domain 始终在确定的 logical thread 上运行，并让 ready、普通 RenderCommand façade 与 Renderer 生命周期按全有或全无的顺序发布。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RenderingThread` | 新增 class | Engine-owned lifecycle controller；GT 发起 start/stop，multi-thread 模式拥有并 join OS thread，logical RT 执行 attach、Renderer startup/teardown callback 与 named-queue pump；不拥有 RenderScene、RHI 或资源策略，也不建立私有 render queue |
| `RenderingThreadMode` | 新增 enum class | 初始化后不可变，表达 MultiThread 或 SingleThread；由 Engine 配置，不作为动态 console 状态 |

本 capability 不新增其他运行时类型。`ThreadStatus`、Task Graph named-thread API、Renderer 与 RenderCommand façade 按各自既有 contract 组合；`RenderingThread` 不通过 getter 暴露 Renderer domain。

## ADDED Requirements

### Requirement: Multi-thread ready handshake 全有或全无
MultiThread 模式的 `start()` MUST 等待 OS thread attach 为 `NamedThread::RenderingThread`、Renderer 启动引导成功、普通 RenderCommand façade 开放成功并发布 ready 后才能向 GT 返回成功。OS thread 仅创建成功、attach 仅成功或 Renderer 内部仅初始化成功都不得被报告为 ready。

启动结果 MUST 在所有成功、可恢复失败和 exception 分支 exactly-once 发布给等待的 GT。发布动作 MUST 建立足够的同步关系，使 GT 在返回后观察到完整 `ThreadStatus` 与 ready 值，而不是并发写入中的部分状态。

#### Scenario: 正常 MultiThread 启动
- **WHEN** OS thread 创建、named-thread attach、Renderer 启动引导和 façade 开放均成功
- **THEN** logical RT MUST 先发布 ready 和成功结果，再进入 named queue pump；GT 收到成功后才可进入主循环

#### Scenario: Attach 失败
- **WHEN** OS thread 无法 attach 到 Task Graph named thread
- **THEN** start MUST 发布失败且不得执行 Renderer 启动引导或开放 façade，Engine MUST 能有界 join/清理该线程

#### Scenario: GT 等待启动结果
- **WHEN** logical RT 正在 attach 或执行 Renderer 启动引导
- **THEN** GT MUST 通过 ready handshake 等待确定结果，不得轮询 thread id、Renderer internal pointer 或 façade 状态猜测是否完成

### Requirement: 启动失败执行完整回滚
启动路径 MUST 是全有或全无。OS thread 创建失败时不得执行 attach；attach 失败时不得执行 Renderer 启动引导；Renderer 启动引导失败时其内部回滚 MUST 在返回失败前完成。若 Renderer 已成功进入 Running 但普通 RenderCommand façade 开放失败，RenderingThread MUST 在 logical RT 请求 Renderer teardown/rollback，保持 façade 关闭和 ready=false，再退出并 join OS thread。

原始创建、attach、Renderer startup 或 façade publication error MUST 保留为 primary `ThreadStatus`；rollback 或 join error只能作为 secondary diagnostic，不得覆盖原始错误。任何失败返回后都不得留下可接受普通命令的 façade 或仍在 pump 的孤立 OS thread。

#### Scenario: Renderer 启动引导失败
- **WHEN** Renderer startup callback 返回失败或抛出异常
- **THEN** callback MUST 已完成其 Renderer-domain failure rollback，RenderingThread MUST 保持 ready=false、保持 façade 关闭并退出/join线程后向 Engine 返回原始错误

#### Scenario: Façade publication 失败
- **WHEN** Renderer 已完成启动引导但 RenderCommand façade 无法开放
- **THEN** logical RT MUST 回滚该 Renderer object，RenderingThread MUST 不发布 ready，并以 façade publication error 为 primary failure完成thread清理

#### Scenario: OS thread factory 失败
- **WHEN** thread factory抛出、返回空对象或无法创建thread
- **THEN** start MUST 直接失败且不得执行 attach、Renderer startup或façade publication

### Requirement: Named queue pump 是唯一运行循环
运行中的 MultiThread RenderingThread MUST 只 pump Task Graph 的 `NamedThread::RenderingThread` queue，不建立私有队列，也不主动扫描 World、Scene 或 resource dirty list。同一 producer 已被 façade 接受的任务 MUST 保持 Task Graph FIFO；空闲等待与新任务/return唤醒 MUST 使用 Task Graph 的 named-thread机制。

#### Scenario: 空闲 RenderingThread
- **WHEN** named queue 无任务
- **THEN** 线程 MUST 使用 Task Graph 等待机制休眠，并能被新 task 或 return request 唤醒

#### Scenario: Return 唤醒空闲 pump
- **WHEN** Renderer teardown 已完成且GT请求RenderingThread return
- **THEN** return request MUST 唤醒等待中的pump，使OS thread有界返回而无需额外哨兵队列

#### Scenario: 同一 producer 连续投递
- **WHEN** GT通过开放façade依次投递Scene update、Draw与Fence
- **THEN** named queue MUST 按接受顺序执行或按terminal规则处置，不得由RenderingThread另行重排

### Requirement: Single-thread 使用同一 logical RT contract
SingleThread 模式 MUST 不创建 OS Rendering Thread，但 SHALL 使用同一个 `RenderingThread` lifecycle controller，在已经 attach 的 GameThread 上以 logical RT 身份执行相同的 Renderer 启动引导、RenderCommand、Draw、ownership disposal 与 teardown contract。同步执行不得成为直接调用 Renderer、RenderScene 或 RHI 的旁路。

`get_thread_id()` 在 SingleThread 模式 MUST 表达“不拥有独立 OS Rendering Thread”，不得伪造OS thread ownership；logical-thread identity由Task Graph当前attach状态决定。

#### Scenario: SingleThread start caller错误
- **WHEN** `start()` 不在已attach的GameThread调用或Task Graph配置与SingleThread不匹配
- **THEN** start MUST失败且不得执行Renderer启动引导或开放façade

#### Scenario: Single-thread Draw
- **WHEN** GT投递Draw command
- **THEN** command MAY同步执行，但必须通过同一个RenderCommand执行路径，并遵守Scene/resource/RHI与RT-side析构contract

#### Scenario: Single-thread command失败
- **WHEN** 同步RenderCommand触发Renderer terminal或ownership disposal
- **THEN** 状态传播、FIFO语义与payload析构线程语义 MUST与MultiThread逻辑路径等价

### Requirement: Stop 先关闭 producer 再 teardown 和 return
正常 `stop()` MUST 先关闭普通 RenderCommand façade，使此前已接受的 FIFO work 保持在 teardown 之前；随后在 logical RT执行Renderer final teardown，发布ready=false，再request return并join OS Rendering Thread。Task Graph shutdown MUST晚于join。

`stop()` MUST可重复调用；已经停止时安全成功。teardown callback失败或抛出异常时仍 MUST尝试有界发布not-ready、request return和join。teardown error为primary；后续return/join错误只能在没有更早错误时成为primary，否则作为secondary diagnostic。

#### Scenario: Pending teardown
- **WHEN** Engine请求关闭且RT仍有accepted work
- **THEN** RenderingThread MUST让这些任务按FIFO执行或按terminal规则处置，再执行Renderer teardown并退出pump

#### Scenario: Teardown callback失败
- **WHEN** logical RT的Renderer teardown返回失败
- **THEN** RenderingThread MUST保留该错误、关闭ready、唤醒并终止pump且尝试join，不得因callback失败留下运行中的thread

#### Scenario: 重复 stop
- **WHEN** Engine或析构安全兜底在RenderingThread已经停止后再次调用stop
- **THEN** 调用 MUST不重复执行Renderer teardown或join已回收thread，并安全返回

### Requirement: 析构不是正常错误通道
Engine正常生命周期 MUST显式调用 `stop()`并检查结果；`RenderingThread`析构只 MAY作为防泄漏的有界安全兜底，不得成为隐藏Renderer teardown或吞掉primary shutdown error的常规路径。

#### Scenario: 正常Engine退出
- **WHEN** Engine拥有的RenderingThread即将析构
- **THEN** Engine MUST已经完成显式stop/join和Task Graph顺序检查，析构不得承担首次业务teardown

## Minimal Implementation Example

> Non-normative：本示例只说明推荐的ownership、GT/logical RT mutable thread、主调用顺序和失败方向；若与Type Contracts或requirements冲突，以规范性内容为准。

```text
MultiThread start on GT:
RenderingThread::start(renderer_startup)
→ create owned OS thread
→ OS thread attaches NamedThread::RenderingThread
→ logical RT executes renderer_startup
→ enable the ordinary RenderCommand façade
→ publish ready=true and the success result exactly once
→ GT receives success
→ OS thread pumps only the Task Graph RenderingThread queue

SingleThread start on attached GameThread:
verify Task Graph maps logical render execution to GameThread
→ execute the same renderer_startup callback
→ enable the same RenderCommand façade
→ publish ready=true
→ later RenderCommand dispatch may execute synchronously through the same contract

Normal MultiThread stop on GT:
disable ordinary RenderCommand acceptance
→ accepted FIFO work remains ordered before the teardown task
→ logical RT executes Renderer final teardown
→ publish ready=false
→ request NamedThread::RenderingThread return
→ wake and exit named queue pump
→ join the owned OS thread
→ Engine may now shut down Task Graph

Failure A — Renderer startup guidance fails:
logical RT preserves the Renderer startup error
→ Renderer startup callback rolls back created Renderer-domain objects
→ keep façade closed and ready=false
→ publish failure exactly once
→ return/join the OS thread
→ GT receives the original failure.

Failure B — façade publication fails after Renderer startup:
keep façade closed and ready=false
→ logical RT performs Renderer rollback/teardown
→ retain façade publication error as primary
→ exit and join the OS thread
→ do not leave Renderer Running without an accessible command path.

Failure C — teardown callback fails:
retain teardown error
→ publish ready=false
→ still request return and join
→ report later join failure only as secondary diagnostic.
```

The framework batch SHALL retain only one MultiThread start/pump/stop smoke, one SingleThread equivalence smoke and one representative startup rollback smoke. Complete thread factory, attach, callback exception, façade publication, teardown, terminal waiter, return wake and join failure coverage belongs to the concentrated Batch D matrix; this capability MUST NOT introduce a separate full fixture for every lifecycle branch.
