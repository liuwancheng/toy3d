## Purpose

定义 Rendering Thread CPU 到达、Game/Render frame lag、显式 flush 和 terminal 只读传播，使普通帧不会把 CPU fence 与 GPU completion 混为一谈。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RenderCommandFence` | 新增 class | GT 发起并等待 Task Graph completion；只表示 RT CPU 到达，不拥有 GPU fence |
| `FrameEndSync` | 新增 class | Engine-owned 双 Fence 轮转与 frame-lag 策略；GT-only mutable state |
| `RenderFenceWaitResult` | 新增 struct | 返回 RT reached、framework failure 或 renderer terminal 的只读结果；不携带 Renderer 指针 |

## ADDED Requirements

### Requirement: Fence 只表示 RT CPU 到达
Fence 完成 MUST 只表示其前序 RenderCommand callable 已在 logical RT 执行或按 terminal 规则处置，不得表示 GPU/present/deferred deletion 完成。

#### Scenario: GPU 尚未完成
- **WHEN** Draw 已 submit 且 RT 到达 Fence，但 queue completion 未达到
- **THEN** Fence MAY 完成，RHI in-flight payload MUST 继续保活

### Requirement: 默认最多领先一帧
FrameEndSync SHALL 使用两个轮转 Fence；默认 frame N 投递完成后等待 frame N-1，zero-lag 模式等待 frame N。

#### Scenario: 默认 FrameEndSync
- **WHEN** GT 完成 frame N 的 Draw 和 Fence 投递
- **THEN** GT MUST 在 frame N-1 Fence 完成后才结束同步边界

### Requirement: 显式 flush
flush rendering commands SHALL 投递专用 Fence 并等待 RT CPU 到达；只有 loading、tool、test、shutdown 等明确边界可进一步要求提交 pending uploads 或等待指定 GPU completion。

#### Scenario: 普通 Gameplay setter
- **WHEN** Gameplay 修改普通材质参数
- **THEN** setter MUST NOT 隐式 flush 或等待 GPU

### Requirement: Terminal 只读传播
Fence/FrameEndSync wait MUST 能观察 first terminal error 或 framework failure，但不得提供 Renderer、RenderScene、resource manager 或 RHI 的可变访问。

#### Scenario: 异步 terminal
- **WHEN** GT 正在等待 Fence 且 RT 锁存 terminal
- **THEN** waiter MUST 被唤醒并获得原始 terminal 结果
