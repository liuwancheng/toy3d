## Purpose

定义 RenderResource 与 RT-only non-owning manager 的状态机、initial payload、pending recording transaction、submit 后发布和安全释放边界，不承担 Asset cache 或身份 registry。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RenderResource` | 新增 polymorphic class | 上层 render representation 独占；RT-only lifecycle；由 manager 调用 RHI init/release；自身不 submit/wait |
| `RenderResourceState` | 新增 enum class | RT-only `Uninitialized/PendingUpload/Ready/Failed/Released` 长期状态 |
| `RenderResourceManager` | 新增 class | Renderer-owned、RT-only、non-owning；跟踪 pending、录制上传并按业务 submit commit/discard |

不新增公开 Prepared/Transaction/Registry/ID/Revision 类型；本次 recording collection 使用 manager 私有标准容器表达。

`RenderResource` 对应 UE4.27 中容易识别的 render-resource 职责，但不复制全局 resource list。`RenderResourceManager` 是 Renderer-owned 的 RT lifecycle coordinator：它确实协调多个 RenderResource 的 pending recording、commit、discard 和 pointer removal，因此 `Manager` 表达稳定职责；它不是 Asset cache、service locator、ownership registry 或全局单例。

## ADDED Requirements

### Requirement: RT-only 状态机
RenderResource 可变状态 MUST 只在 logical RT 访问，并按 init、recording、business submit、deterministic failure 和 release 转换；GT 不得轮询 Ready 决定普通帧行为。无效 descriptor、Unsupported capability 或不可重试的 resource-local failure MAY 进入 Failed并从 pending collection 移除；frame abort、list discard 或 business submit failure MUST 保持 PendingUpload 以便重录。

#### Scenario: Begin init
- **WHEN** RT 接受资源初始化
- **THEN** resource MUST 进入 PendingUpload 并加入 manager non-owning pending collection

#### Scenario: Unsupported resource contract
- **WHEN** resource init/recording 发现当前 RHI profile 明确不支持其 descriptor 或必要 capability
- **THEN** resource MUST 进入 Failed、从 manager pending collection 摘除并保留原始可诊断错误，不得以空 upload成功

### Requirement: record_pending_uploads 使用私有 recording collection
`RenderResourceManager::record_pending_uploads()` MUST 在当前 graphics context 中遍历稳定的 pending pointer snapshot，让各 resource 依次创建空 RHI resource、录制 upload 和必要 transition，并把本次成功录制的 resource 记录到 manager 私有 recording collection。RenderResource 自身 MUST NOT 创建 command context、submit、present、flush 或 wait。

任一必要 resource recording 失败时，manager MUST 返回可诊断失败；调用方 MUST 放弃后续业务 pass 并 abort/discard 当前 frame/list。该 list 内任何已经成功录制的 resource 都不得因此提前发布 Ready。

#### Scenario: 两个 pending resources 录制成功
- **WHEN** manager 在当前 context 依次成功录制 Mesh buffer 与 Texture upload/transition
- **THEN** 两个 resource MUST 仍保持 PendingUpload，但 MAY 按其各自 contract 在当前 recording 的后续命令中局部使用

#### Scenario: 第二个 resource 录制失败
- **WHEN** 第一个 resource 已录制成功而第二个 resource recording 失败
- **THEN** manager MUST 报告失败并使整个当前 recording collection只能 discard；第一个 resource MUST NOT 单独发布 Ready

### Requirement: 当前 recording eligibility 不成为长期状态
处于 PendingUpload 的 resource 只有在本次创建、upload、transition 和上层 layout validation 全部成功，且使用严格位于这些命令之后时，才 MAY 被当前同一 list局部消费。该 eligibility MUST 只由 manager 私有 recording collection和当前 command ordering表达，不新增 `Prepared` enum、transaction object、token、跨帧 handle 或公开 query。

#### Scenario: StaticMesh 同帧首次 Draw
- **WHEN** StaticMesh 必要 RenderResources 已在当前 list 成功录制且完整 StaticMeshRenderData gate通过
- **THEN** Base Pass MAY 在当前 list 后续录制 Draw，但这些 resources 在 submit 成功前 MUST 仍是 PendingUpload

### Requirement: Initial payload 保留到 submit 成功
upload 调用返回前 MUST 把调用方数据复制到 RHI staging；immutable CPU initial payload MUST 保留到包含它的业务 list 成功提交。

#### Scenario: Frame abort
- **WHEN** upload 已录制但 frame abort
- **THEN** recording staging MAY 丢弃，resource MUST 保持 PendingUpload，initial payload MUST 可重录

#### Scenario: Submit 成功
- **WHEN** 业务 list submit 成功
- **THEN** manager MUST 发布 Ready 并允许释放 initial payload

### Requirement: Manager 以 business submit 原子 commit/discard recording
业务 list submit 成功后，manager MUST 只对仍存活且仍登记在本次 recording collection中的 resources 发布 Ready并允许释放 initial payload；该 commit 不等待 GPU completion。frame abort、list discard、明确 submit failure 或 submit 前 resource release MUST 从 recording collection discard对应 non-owning entries，不发布 Ready。

submit 成功后 present 返回 Suboptimal、OutOfDate 或 terminal时，manager MUST 保留已经完成的 Ready publication；presentation result 不得回滚业务 submit。若 backend 无法判断 submit 是否产生 GPU work，Renderer MUST 进入 terminal 路径，manager MUST 先清空 non-owning collections并停止后续解引用。

#### Scenario: Submit success 后 present OutOfDate
- **WHEN** 包含 pending uploads 的 business list submit 成功而 present 返回 OutOfDate
- **THEN** manager MUST 将仍登记的 resources发布为 Ready，只把 presentation resources留待后续有效 frame重建

#### Scenario: Submit 明确失败
- **WHEN** backend 明确没有向 GPU queue提交业务 list
- **THEN** manager MUST discard当前 recording collection，resources保持 PendingUpload且 initial payload可重录

### Requirement: Manager non-owning
Manager MUST NOT 拥有 Asset/render representation，不得承担 Asset cache、ID registry、Scene/Material 规则、backend allocation 或 deferred deletion。

#### Scenario: Pending release
- **WHEN** PendingUpload resource 被释放
- **THEN** manager MUST 先从 pending snapshot、pending collection和当前 recording collection移除全部 non-owning pointer，再允许 owning representation析构

#### Scenario: 已录制 upload 但 submit 前 release
- **WHEN** resource upload已进入当前 command list，而排序正确的 release command在 business submit前执行
- **THEN** manager MUST 从当前 recording collection摘除该 resource并禁止后续 Ready commit；command context/list MUST 独立保活已经录制的实际 RHI/staging payload

### Requirement: Release 不等待 GPU
正常 release MUST 在 RT 释放 RHI 强引用并析构 representation；in-flight command list 保活实际 RHI object 到 completion，普通路径不得 wait idle。

#### Scenario: 旧 list 仍使用资源
- **WHEN** release command 执行时 GPU 尚未完成旧 Draw
- **THEN** native resource MUST 由 RHI in-flight/deferred deletion 保活

### Requirement: Terminal 先清 non-owning pointer
manager 进入 terminal MUST discard current recording、清空 pending snapshot、pending collection和 recording collection中的所有 non-owning pointer，并保证之后不再解引用 RenderResource。清理这些 pointer后，pending owner command payload MAY 在 logical RT terminal skip/disposal路径析构。

#### Scenario: 后续 owning command 被 skip
- **WHEN** terminal 后 pending command payload 析构 representation
- **THEN** manager MUST 已无指向它的记录

## Minimal Implementation Example

> Non-normative：本示例只说明推荐的 ownership、RT mutable thread、主调用顺序和失败方向；若与 Type Contracts 或 requirements 冲突，以规范性内容为准。

```text
GT:
Asset/render lifecycle command owns or transfers the representation;
GT never reads RenderResourceState.

RT init commands:
PositionVertexBuffer enters PendingUpload
→ a texture RenderResource enters PendingUpload
→ RenderResourceManager stores non-owning pending pointers only

RT valid frame recording:
begin_frame()
→ create one graphics context
→ RenderResourceManager::record_pending_uploads()
     → create empty RHI buffers/texture
     → upload call copies source data into RHI-owned staging before return
     → record RHIAccess transitions
     → store successful resources in the private recording collection
→ complete StaticMeshRenderData gate
→ current list may record Base Pass after upload/transition
→ finish immutable list

Business submit success:
queue accepts the list
→ manager publishes still-registered resources Ready
→ release their CPU initial payload
→ present result cannot roll this publication back
→ command list keeps actual staging/RHI resources alive until completion

Failure A — frame abort or list discard:
discard the private recording collection
→ keep resources PendingUpload
→ retain initial payload for the next valid frame.

Failure B — second resource recording fails:
abort the whole business recording
→ do not commit the first resource independently.

Failure C — PendingUpload release after its upload was recorded:
remove the resource from every manager non-owning collection
→ destroy the owning representation on RT
→ do not publish Ready after submit
→ command-list strong refs keep already-recorded GPU payload safe.

Failure D — terminal skip:
manager first clears all non-owning collections
→ logical RT disposal may then destroy skipped ownership payloads
→ manager performs no later dereference.
```

Batch B/C SHALL retain only one focused manager recording/commit/discard smoke in addition to existing RHI upload/state coverage. Full same-frame Mesh upload→Base Pass→submit/present→GPU completion, retry, release and terminal paths belong to the concentrated Batch D end-to-end test flow; this capability MUST NOT create a separate large fixture for every state transition.
