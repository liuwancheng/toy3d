## Purpose

定义 RenderResource 与 RT-only non-owning manager 的状态机、initial payload、pending recording transaction、submit 后发布和安全释放边界，不承担 Asset cache 或身份 registry。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RenderResource` | 新增 polymorphic class | 上层 render representation 独占；RT-only lifecycle；由 manager 调用 RHI init/release；自身不 submit/wait |
| `RenderResourceState` | 新增 enum class | RT-only `Uninitialized/PendingUpload/Ready/Failed/Released` 长期状态 |
| `RenderResourceManager` | 新增 class | Renderer-owned、RT-only、non-owning；跟踪 pending、录制上传并按业务 submit commit/discard |

不新增公开 Prepared/Transaction/Registry/ID/Revision 类型；本次 recording collection 使用 manager 私有标准容器表达。

## ADDED Requirements

### Requirement: RT-only 状态机
RenderResource 可变状态 MUST 只在 logical RT 访问，并按 init、business submit、failure 和 release 转换；GT 不得轮询 Ready 决定普通帧行为。

#### Scenario: Begin init
- **WHEN** RT 接受资源初始化
- **THEN** resource MUST 进入 PendingUpload 并加入 manager non-owning pending collection

### Requirement: Initial payload 保留到 submit 成功
upload 调用返回前 MUST 把调用方数据复制到 RHI staging；immutable CPU initial payload MUST 保留到包含它的业务 list 成功提交。

#### Scenario: Frame abort
- **WHEN** upload 已录制但 frame abort
- **THEN** recording staging MAY 丢弃，resource MUST 保持 PendingUpload，initial payload MUST 可重录

#### Scenario: Submit 成功
- **WHEN** 业务 list submit 成功
- **THEN** manager MUST 发布 Ready 并允许释放 initial payload

### Requirement: Manager non-owning
Manager MUST NOT 拥有 Asset/render representation，不得承担 Asset cache、ID registry、Scene/Material 规则、backend allocation 或 deferred deletion。

#### Scenario: Pending release
- **WHEN** PendingUpload resource 被释放
- **THEN** manager MUST 先移除 non-owning pointer，再允许 owning representation 析构

### Requirement: Release 不等待 GPU
正常 release MUST 在 RT 释放 RHI 强引用并析构 representation；in-flight command list 保活实际 RHI object 到 completion，普通路径不得 wait idle。

#### Scenario: 旧 list 仍使用资源
- **WHEN** release command 执行时 GPU 尚未完成旧 Draw
- **THEN** native resource MUST 由 RHI in-flight/deferred deletion 保活

### Requirement: Terminal 先清 non-owning pointer
manager 进入 terminal MUST discard current recording、清空所有 pending non-owning pointer，并保证之后不再解引用 RenderResource。

#### Scenario: 后续 owning command 被 skip
- **WHEN** terminal 后 pending command payload 析构 representation
- **THEN** manager MUST 已无指向它的记录
