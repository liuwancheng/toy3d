## Purpose

定义 Texture Asset 与地址稳定的 Texture render representation、内容更新、native replacement、candidate 发布和 Material binding cache 失效边界。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `TextureRenderResource` | 新增 RenderResource | Texture-owned stable representation；RT 管理 active/candidate texture/view 与 binding generation；MaterialProxy non-owning 引用 |

不新增公共 `TextureReference`、Texture ID、candidate wrapper 或跨线程 revision 类型；candidate 使用 `TextureRenderResource` 私有 RHI refs 表达。

## ADDED Requirements

### Requirement: Stable representation address
Texture Asset SHALL 拥有地址稳定的 TextureRenderResource，MaterialRenderProxy MAY 保存 non-owning pointer；GT Texture/Material 状态 MUST 保持 Asset 强引用覆盖 RT 使用期。

#### Scenario: Material 切换 Texture
- **WHEN** GT 将材质参数切换到另一 Texture
- **THEN** 新 Asset 强引用 MUST 先建立，proxy update MUST 排在旧 Asset 引用释放之前

### Requirement: 内容更新不替换 native view
同一 native texture 的内容更新 MUST 只录制 upload/transition，不递增 binding generation，不强制重建 Material binding。

#### Scenario: 更新一个 mip 内容
- **WHEN** descriptor、format 和 view identity 不变
- **THEN** active texture/view MUST 保持，只有 GPU 内容更新

### Requirement: Native replacement 使用 candidate
descriptor、mip 结构、format 或 hot reload 需要替换 native texture/view 时，active MUST 保持可用，candidate 仅在业务 submit 成功后发布。

#### Scenario: Candidate submit 失败
- **WHEN** candidate list 未成功 submit
- **THEN** active MUST 不变，candidate MUST 丢弃或进入明确重试状态

#### Scenario: Candidate submit 成功
- **WHEN** candidate 成功 submit
- **THEN** candidate MUST 成为 active，RT-only binding generation MUST 递增

### Requirement: Binding 保活实际 view
Material binding 与 command list MUST 强引用解析时的实际 RHI view/resource，使后续 active replacement 不改变已录制 GPU 工作。

#### Scenario: Draw 后替换 Texture
- **WHEN** 旧 view 已录制进 Draw 且 Texture 发布新 active view
- **THEN** 旧 view MUST 保活到对应 queue completion
