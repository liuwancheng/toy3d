## Purpose

定义 command-list local resource state、queue committed state、GPU completion 和 deferred deletion 的独立权威，保证录制丢弃或提交失败不会污染后续资源状态。

## Type Contracts

本 capability 不新增公共类型；沿用现有 `RHIQueueCompletionValue`、resource/view refs、command-list state tracker 和 deferred deletion 设施。若实现需要新的具名 state summary 或 submission record，MUST 先补充本表。

## ADDED Requirements

### Requirement: Recording 使用 local state
transition 录制 MUST 只修改 command-list local tracker，至少记录 first required access、local access 和 final access；不得提前修改 resource/global committed state。

#### Scenario: Command list discard
- **WHEN** list 录制完成后被丢弃
- **THEN** 其 final access MUST 不影响 committed state

### Requirement: Committed state 按实际 submit 顺序推进
同一 graphics queue 的 committed state MUST 按实际成功 submit 顺序发布，无需等待 GPU completion。

#### Scenario: 录制顺序不同于提交顺序
- **WHEN** list A/B 按 A→B 录制但按 B→A submit
- **THEN** state validation/commit MUST 以 B→A 为权威顺序

### Requirement: Completion 只控制生命周期回收
Queue completion value SHALL 只表示 GPU 已执行到提交点，用于 command list、staging、descriptor、allocator、RHI object 和 deferred deletion 回收；不得代替 CPU Fence 或 committed ordering。

#### Scenario: Commit 后 GPU 未完成
- **WHEN** resource 已 Ready 且 committed access 已发布，但 completion 未达到
- **THEN** 后续 queue work MAY 使用该状态，旧 in-flight payload MUST 继续保活

### Requirement: RHI object 具有 device ownership identity
每个 RHI object MUST 能验证属于创建它的 device；跨 device 录制或 submit MUST 在 native 调用前失败。

#### Scenario: 错误 device resource
- **WHEN** context 录制另一个 device 创建的 view/resource
- **THEN** validation MUST 返回可诊断错误且不得产生 native work

### Requirement: D3D11 completion 是 GPU 进度
D3D11 completion MUST 使用 FL11_0 event query 或等价 GPU 信号，不得把 CPU `ExecuteCommandList` 返回视为 GPU 完成。

#### Scenario: D3D11 deferred deletion
- **WHEN** CPU 已执行 D3D11 command list 但 event query 未完成
- **THEN** native resource MUST 不得回收
