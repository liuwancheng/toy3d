## Purpose

定义 Game/Render 框架依赖的进程级 Task Graph active instance、Named Thread 路由和显式 shutdown 行为，使所有框架任务共享同一个可诊断调度域。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `TaskGraphInterface` | 现有修改 | composition root 独占；提供受控 active access；内部同步；不隐式构造，不拥有 Renderer |

本 capability 不新增其他具名类型；沿用现有 GraphTask、GraphEvent、NamedThread 和 status 类型。

## ADDED Requirements

### Requirement: 单一 active instance
进程 MUST 同时最多存在一个 active Task Graph instance；实例由 Engine 显式创建和 shutdown，访问入口只在 active 生命周期内有效。

#### Scenario: 重复启动
- **WHEN** active instance 存在时再次启动 Task Graph
- **THEN** 系统 MUST 返回可诊断错误且不得替换现有 instance

#### Scenario: shutdown 后访问
- **WHEN** Task Graph 已 shutdown
- **THEN** `is_running()` MUST 为 false，访问 MUST fail fast 或返回明确 stopped 状态，不得自动重建

### Requirement: Named Thread transport 可用性
GameThread 与 RenderingThread MUST 在普通 RenderCommand 开放前完成 attach；Task Graph MUST 为同一 producer 到同一 named queue 保证 FIFO。

#### Scenario: RenderingThread 尚未 attach
- **WHEN** 调用方尝试向 RenderingThread named queue 投递正式命令
- **THEN** 框架 MUST 拒绝投递并记录生命周期诊断

### Requirement: 显式 shutdown
Task Graph MUST 在 RenderingThread join 后由 Engine 显式 shutdown，并保证所有已接受 tracked task 发布一次 completion outcome。

#### Scenario: Engine 正常关闭
- **WHEN** RenderingThread 已完成 teardown 并 join
- **THEN** Engine MUST shutdown Task Graph，唤醒 waiter，并在销毁前验证无 active named thread
