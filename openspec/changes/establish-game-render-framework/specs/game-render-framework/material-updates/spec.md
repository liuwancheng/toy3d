## Purpose

定义 Material、MaterialInstance 与 MaterialRenderProxy 的所有权、普通动态参数 FIFO 更新、Draw 前按需物化和结构性 candidate replacement 边界。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `MaterialInstance` | 现有修改 | GT-owned mutable overrides；拥有 MaterialRenderProxy representation 与 Texture Asset 强引用；setter 不操作 RHI |
| `MaterialRenderProxy` | 新增 class | MaterialInstance-owned stable representation；RT-only 参数表、dirty state、ShaderMap ref 与 binding cache；不继承 RenderResource |

沿用 Shader 系统的 `ShaderParameterId`、ShaderMap 和 RHI binding 类型；不新增普通 setter revision 或 Material resource ID。

## ADDED Requirements

### Requirement: 普通 setter 只投递 owned value
scalar、vector 和 texture setter SHALL 更新 GT state，并投递 stable proxy identity、parameter identity 与 owned value；不得创建 RHI buffer、submit、flush 或 wait。

#### Scenario: 连续 setter
- **WHEN** Draw 前同一参数被连续修改
- **THEN** RT MUST 按 FIFO 应用，Draw MUST 使用最终值

### Requirement: Draw 前按需物化
RT SHALL 在 Draw 前解析 instance override/parent default、生成 frame-local constants、解析 TextureRenderResource 当前 view 并物化 Material binding；多个 dirty update MAY 合并为一次构建。

#### Scenario: 未参与 Draw 的 dirty Material
- **WHEN** MaterialProxy 被更新但本帧无可见 Primitive 使用
- **THEN** 系统 MUST NOT 为其强制创建 frame-local constants/binding

### Requirement: Texture 引用切换安全
MaterialInstance GT state MUST 持有 Texture Asset 强引用；proxy texture pointer 只允许 RT 解引用，更新/释放顺序由专用 setter 保证。

#### Scenario: 旧 Texture 最后引用释放
- **WHEN** texture setter 切换引用且旧 Texture 无其他 GT owner
- **THEN** proxy update MUST 排在旧 Texture release command 之前

### Requirement: 结构性变化走 replacement
blend mode、two-sided、depth policy、shading model、static switch、shader permutation、binding layout 或 vertex input requirement 变化 MUST NOT 走普通 setter，必须构建完整 candidate ShaderMap/Proxy 后替换。

#### Scenario: Static switch 修改
- **WHEN** MaterialInstance 修改 static switch
- **THEN** 当前 proxy MUST 保持可用，candidate 未完成前不得发布不完整 shader/binding state
