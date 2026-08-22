## Purpose

定义 Renderer lifecycle、first terminal error、pending command disposal、正常 drain、DeviceLost 降级和 Engine/RenderingThread/Task Graph 的最终关闭顺序。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RendererLifecycleState` | 新增 enum class | `Stopped/Starting/Running/Stopping/Terminal`；跨线程原子发布，不表达详细错误 |
| `RenderingStatus` | 新增 immutable struct | first terminal/framework error 的只读副本；由 Renderer latch，通过 Fence/FrameEndSync 观察，不授予服务访问 |

## ADDED Requirements

### Requirement: First-error latch
Renderer MUST 原子发布 terminal lifecycle 并永久保留第一个原始 terminal/framework error；secondary cleanup error 只能作为附加诊断。

#### Scenario: DeviceLost 后 wait failure
- **WHEN** DeviceLost 先发生且 shutdown wait 再失败
- **THEN** Game Thread 观察到的 primary error MUST 仍为 DeviceLost

### Requirement: Terminal 先使 non-owning 状态安全
进入 terminal MUST 先停止新 frame/resource init，abort current recording，并让 ResourceManager 清除全部 non-owning pending pointer；之后才能 skip pending callable并析构 payload。

#### Scenario: Pending resource owner 被 skip
- **WHEN** pending command 持有 resource representation ownership
- **THEN** manager MUST 已停止解引用，payload MUST 在 logical RT 析构

### Requirement: Pending command 确定处置
terminal 后已接受 RenderCommand MUST 继续 FIFO 出队、跳过 callable并在 logical RT 析构 payload；Fence MUST 完成并报告 terminal。

#### Scenario: Draw 与 Fence pending
- **WHEN** terminal 发生在 Draw 前
- **THEN** Draw SceneRenderer MUST 不执行但安全析构，后续 Fence MUST 唤醒 GT

### Requirement: 正常 shutdown drain
正常 shutdown MUST 停止 GT producer，排队 World/Proxy remove 和 resource release，执行最终内部 Renderer teardown，等待 RT CPU 到达，再 request return、join RenderingThread 和 shutdown Task Graph。

#### Scenario: 正常退出有 pending uploads
- **WHEN** Engine 请求退出且资源仍 PendingUpload
- **THEN** RT teardown MUST discard pending recording、release representation/RHI refs并完成确定 shutdown

### Requirement: RT teardown 顺序
RT teardown MUST abort active frame、清 RenderScene、terminal/clear ResourceManager、释放 Renderer-owned refs、处理 queue idle/completed/deferred deletion、销毁 viewport/placeholder/device，最后清 façade binding。

#### Scenario: 正常 RHI teardown
- **WHEN** device 非 lost
- **THEN** queue wait/回收 MUST 在 native device 销毁前完成

### Requirement: DeviceLost 不无限阻塞
DeviceLost 或状态未知同步失败后 shutdown MUST 不无限 retry/wait idle；应停止进一步不安全 native 调用，释放 CPU wrappers并走 backend 允许的 terminal teardown。

#### Scenario: wait idle 返回 DeviceLost
- **WHEN** terminal teardown 无法确认 GPU idle
- **THEN** 系统 MUST 记录 secondary diagnostic并继续有限 teardown，不得挂死 Engine exit
