# game-render-framework/rhi-resource-state Specification

## Purpose
定义 command-list local resource state、queue committed state、GPU completion 和 deferred deletion 的独立权威，保证录制丢弃或提交失败不会污染后续资源状态。

## Requirements

### Requirement: Recording 使用 local state
transition 和 resource usage 录制 MUST 只修改 command-list local tracker，至少记录 first required access、当前 local access 和 final access；不得提前修改 resource/global committed state。buffer 第一阶段按完整资源跟踪；texture tracker MUST 保留演进到 subresource range 的边界，不得由上层记录 Vulkan layout 或 D3D12 native state。

#### Scenario: Command list discard
- **WHEN** list 录制完成后被丢弃
- **THEN** 其 final access MUST 不影响 committed state

#### Scenario: 同一 list upload 后 Draw
- **WHEN** list 先把一个 buffer 从 first required access 转为 `RHIAccess::CopyDestination`、录制 upload，再转为 `RHIAccess::VertexBuffer` 或 `RHIAccess::IndexBuffer` 并录制 Draw
- **THEN** local tracker MUST 按该顺序验证完整路径，Draw MAY 使用同一 list 中先前完成录制的 upload，无需等待 GPU completion

#### Scenario: Recording 中途失败
- **WHEN** upload 已录制但后续 transition、pipeline validation 或 Draw 录制失败
- **THEN** 整个 list 的 local final state MUST 被丢弃，不得部分写入 queue committed state

### Requirement: Committed state 按实际 submit 顺序推进
同一 graphics queue 的 committed state MUST 按实际成功 submit 顺序发布，无需等待 GPU completion。submit 时 queue MUST 以当时的 committed state 与 list 的 first required access 做 validation/reconciliation，再决定必要的 backend-compatible submission ordering 或明确失败；不得假定录制顺序等于提交顺序，也不得让 renderscene 生成 backend-specific seam barrier。

#### Scenario: 录制顺序不同于提交顺序
- **WHEN** list A/B 按 A→B 录制但按 B→A submit
- **THEN** state validation/commit MUST 以 B→A 为权威顺序

#### Scenario: Initial access 无法 reconciliation
- **WHEN** list 的 first required access 与当前 committed state 不兼容，且 backend 无法在不重录业务命令的前提下安全衔接
- **THEN** submit MUST 返回可诊断失败，禁止静默采用过期 initial state

### Requirement: Submit 结果控制 committed state 发布
只有 backend 明确报告 business submit 成功后，queue 才能发布该 list 的 final access 并返回有效 completion value。明确未产生 GPU 工作的 submit failure MUST 丢弃 local final state；若 backend failure 发生在可能已经产生 GPU 工作且无法确认边界的位置，Renderer/RHI MUST 锁存 terminal failure，禁止回滚后继续使用状态未知的资源。

submit 成功后的 present `Suboptimal`、`OutOfDate` 或 terminal result MUST NOT 回滚已经发布的 committed state、RenderResource Ready 或 GPU payload ownership。

#### Scenario: Submit 明确失败
- **WHEN** backend 在任何 GPU 工作进入 queue 前明确拒绝 submit
- **THEN** committed state MUST 保持 submit 前值，list local state MUST 被丢弃，pending resource MUST 保持可重录

#### Scenario: Submit 是否执行未知
- **WHEN** backend failure 无法证明 GPU 未开始执行该 submission
- **THEN** Renderer MUST 进入 terminal 路径，不得把 committed state 恢复为旧值后继续录制或提交

#### Scenario: Submit 成功但 present OutOfDate
- **WHEN** business list submit 成功后 present 返回 OutOfDate
- **THEN** queue committed state 和 resource Ready publication MUST 保留，只把 presentation resources 标记为后续有效 frame 重建

### Requirement: PendingUpload 只在当前 recording 局部可用
处于 `PendingUpload` 的 RenderResource 只有在创建成功、upload 和必要 transition 已录制、上层 layout/VertexFactory validation 成功，且使用严格位于这些命令之后时，才 MAY 在当前同一 command list 内局部使用。该局部资格不是新的长期状态、公共 enum 或跨帧 promise。

只有包含该资源的 business submit 成功后，`RenderResourceManager` 才能发布长期 `Ready` 并允许释放 CPU initial payload。frame abort、list discard 或 submit failure MUST 保持 `PendingUpload` 和可重录 payload。

#### Scenario: Upload 与 StaticMesh Base Pass 同帧
- **WHEN** StaticMesh 必要 buffers 的 upload/transition 和 LocalVertexFactory validation 都在 Base Pass 前成功录制
- **THEN** 对应 MeshBatch MAY 在当前 list 绘制，但其 RenderResources 只能在该 list submit 成功后成为长期 Ready

#### Scenario: Frame abort
- **WHEN** PendingUpload resource 已在当前 list 局部可用但 frame 在 submit 前 abort
- **THEN** resource MUST 保持 PendingUpload，initial payload MUST 保留，局部资格 MUST 随被丢弃的 recording 一同失效

### Requirement: Completion 只控制生命周期回收
Queue completion value SHALL 只表示 GPU 已执行到提交点，用于 command list、staging、descriptor allocation、command/allocator pool、pipeline/binding/resource strong refs、RHI object 和 deferred deletion 回收；不得代替 CPU Fence、RenderResource Ready 或 committed ordering。录制单元与 backend payload MUST 保活其实际引用，不能只依赖上层 representation 仍然存在。

#### Scenario: Commit 后 GPU 未完成
- **WHEN** resource 已 Ready 且 committed access 已发布，但 completion 未达到
- **THEN** 后续 queue work MAY 使用该状态，旧 in-flight payload MUST 继续保活

### Requirement: RHI object 具有 device ownership identity
每个 RHI object MUST 能验证属于创建它的 device；resource、view、shader、binding、pipeline、render-pass attachment 与 command list 的跨 device 录制或 submit MUST 在 native 调用前失败。公共 validation 不得依赖 backend downcast 猜测 owner。

#### Scenario: 错误 device resource
- **WHEN** context 录制另一个 device 创建的 view/resource
- **THEN** validation MUST 返回可诊断错误且不得产生 native work

### Requirement: D3D11 completion 是 GPU 进度
D3D11 completion MUST 使用 FL11_0 event query 或等价 GPU 信号，不得把 CPU `ExecuteCommandList` 返回视为 GPU 完成。

#### Scenario: D3D11 deferred deletion
- **WHEN** CPU 已执行 D3D11 command list 但 event query 未完成
- **THEN** native resource MUST 不得回收

### Requirement: 三后端保持同一公共状态语义
Vulkan backend SHALL 把公共 access 转换为 image layout、pipeline stage 与 access barrier；D3D12 backend SHALL 转换为 resource state/barrier；D3D11 backend SHALL 跟踪逻辑 access 并在需要时解除 SRV/RTV/UAV 等冲突绑定。转换 MUST 集中在 backend，公共枚举不得依赖 native 数值相同强制转换。

任一 backend 不支持所需 access、format usage 或安全 reconciliation 时 MUST 返回 `Unsupported` 或等价可诊断错误，不得无操作后成功。`Vulkan ES3.1 profile` 路径不得依赖 Vulkan 1.2+ 或可选同步特性抬高移动端基线。

#### Scenario: D3D11 隐式状态 API
- **WHEN** 公共 transition 从 `RHIAccess::ShaderResourceGraphics` 切换到 `RHIAccess::RenderTarget`
- **THEN** D3D11 backend MUST 更新逻辑 committed state 并解除冲突 SRV binding，不能因 D3D11 没有显式 resource-state barrier 而忽略公共语义
