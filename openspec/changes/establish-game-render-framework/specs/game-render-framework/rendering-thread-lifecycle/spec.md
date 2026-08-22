## Purpose

定义独立 Rendering Thread 与 single-thread logical fallback 的启动握手、Named Thread pump、返回和 join，使 Renderer domain 始终在确定的 logical thread 上运行。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RenderingThread` | 新增 class | Engine-owned lifecycle controller；GT 启停，multi-thread 模式拥有 OS thread，logical RT 执行 attach/pump/Renderer lifecycle；不拥有 RenderScene/RHI 策略 |
| `RenderingThreadMode` | 新增 enum class | 初始化后不可变，表达 MultiThread 或 SingleThread；由 Engine 配置，不作为动态 console 状态 |

## ADDED Requirements

### Requirement: Multi-thread ready handshake
multi-thread 模式 MUST 等待 OS thread attach 为 RenderingThread 并完成 Renderer bootstrap 后才报告 ready 和开放 façade。

#### Scenario: Attach 失败
- **WHEN** OS thread 无法 attach 到 Task Graph named thread
- **THEN** start MUST 失败，Renderer bootstrap MUST 不执行，Engine MUST 可安全 join/清理该线程

### Requirement: Named queue pump
运行中的 RenderingThread MUST 只 pump Task Graph named queue，不建立私有队列，也不主动扫描 World、Scene 或 resource dirty list。

#### Scenario: 空闲 RenderingThread
- **WHEN** named queue 无任务
- **THEN** 线程 MUST 使用 Task Graph 等待机制休眠，并能被新 task 或 return request 唤醒

### Requirement: Single-thread 等价路径
single-thread 模式 MUST 不创建 OS Rendering Thread，但 SHALL 使用同一个 RenderingThread lifecycle controller 在 Game Thread 初始化、执行和 teardown Renderer domain。

#### Scenario: Single-thread Draw
- **WHEN** GT 投递 Draw command
- **THEN** command MUST 立即通过 logical RT 路径执行，且不得绕过 Scene/resource/RHI contract

### Requirement: Return 与 join 顺序
正常关闭 MUST 在 Renderer teardown 完成后 request return 并 join OS Rendering Thread；Task Graph shutdown MUST 晚于 join。

#### Scenario: Pending teardown
- **WHEN** Engine 请求关闭且 RT 仍有 accepted work
- **THEN** RenderingThread MUST 先按 shutdown capability drain/teardown，再退出 pump
