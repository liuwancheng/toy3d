## Purpose

定义 Material、MaterialInstance 与 MaterialRenderProxy 的所有权、普通动态参数 FIFO 更新、Draw 前按需物化和结构性 candidate replacement 边界。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `MaterialInstance` | 现有修改 | GT-owned mutable overrides；拥有地址稳定但 GT 不解引用其 RT state 的 MaterialRenderProxy allocation，以及覆盖 RT 使用期的 Texture Asset 强引用；setter 不操作 RHI |
| `MaterialRenderProxy` | 新增 class | MaterialInstance-owned stable RT representation；RT-only 参数表、dirty state、ShaderMap ref 与 Material binding cache；不继承 RenderResource，不拥有 Texture Asset，不选择 Mesh/VertexFactory |
| `ShaderGraphicsPassState` | 新增 stateless value type | UE4.27 可识别的 Shader Pass render-state template；Toy3d 中声明于 backend-neutral Shader format 层，以封闭的 nested enum/value 字段保存 topology、rasterization、depth/stencil、blend 与 color-write state，并随 ShaderMapEntry 持久化；不包含 attachment format、sample count、RHI object 或 Material policy。不能复用 `RHIGraphicsPipelineDesc`，因为后者属于 runtime RHI 且还包含 Shader、vertex layout 与 attachment compatibility；其 nested enum 只表达 Shader format 的稳定序列化域，不泄漏 native API 数值 |

沿用 Shader 系统的 `ShaderParameterId`、ShaderMap 和 RHI binding 类型；不新增普通 setter revision 或 Material resource ID。

`MaterialRenderProxy` 对应 UE4.27 中容易识别的 Material render proxy 职责，但只表达 Toy3d 的 GT/RT 隔离：MaterialInstance 在 GT 可变，Proxy 在 RT 可变并为 Draw 提供 Material group。它不是为绕过 C++ 构造、访问控制或智能指针限制增加的 wrapper。

## ADDED Requirements

### Requirement: 普通 setter 只投递 owned value
scalar、vector 和 texture setter SHALL 先使用 Material/ShaderMap parameter contract 验证 `ShaderParameterId` 与 value type，再更新 GT override，并投递 stable `MaterialRenderProxy` identity、parameter identity 与 owned value。setter 不得创建 RHI buffer、material binding、submit、flush 或 wait；未知 parameter、类型不兼容或 Texture Asset 无效时 MUST 返回可诊断失败且不得投递部分 update。

#### Scenario: 连续 setter
- **WHEN** Draw 前同一参数被连续修改
- **THEN** RT MUST 按 FIFO 应用，Draw MUST 使用最终值

#### Scenario: 参数类型错误
- **WHEN** scalar setter 指向 ShaderMap 中的 texture parameter 或未知 ShaderParameterId
- **THEN** MaterialInstance MUST 保持原 GT override、不 enqueue update并返回可诊断失败

### Requirement: MaterialRenderProxy ownership 通过 release command 闭合
MaterialInstance SHALL 独占地址稳定的 `MaterialRenderProxy` allocation，GT 只把其地址作为受 RenderCommand FIFO 和 shutdown drain 保护的 opaque identity，不得读取或修改 Proxy 的 RT state。最终释放前，所有引用该 Proxy 的 `StaticMeshSceneProxy` material update/remove MUST 先 enqueue；随后 MaterialInstance MUST 把 Proxy ownership 移入最后一条 release RenderCommand，由 RT 析构。

正常 shutdown MUST drain 引用移除与 Proxy release；terminal skip/disposal MUST 安全析构尚未执行的 ownership payload。不得为长期 ownership 新增 Material registry、ID 或 shared mutable cache。

#### Scenario: 最后一个 MaterialInstance owner 释放
- **WHEN** 最后一个 GT MaterialInstance 强引用释放且仍有 StaticMeshSceneProxy 使用其 MaterialRenderProxy
- **THEN** 对应 Proxy material update/remove MUST 先进入 FIFO，MaterialRenderProxy ownership-transfer release MUST 最后进入，并由 RT 按该顺序执行

### Requirement: Draw 前按需物化
RT SHALL 只在一个可见 `MeshBatch` 实际使用该 `MaterialRenderProxy` 时，解析 instance override/parent Material default、依据 ShaderMap parameter layout 生成 frame-local constants、解析 `TextureResource` 当前 active view/binding generation，并物化或复用 Material logical binding set；多个 dirty FIFO update MAY 合并为一次构建。

frame-local constants、descriptor/physical packet 和最终 RHI binding MUST 由当前 recording/command list 保活到 queue completion。MaterialRenderProxy 自身不得直接 submit、等待 GPU 或持有 viewport frame token。

#### Scenario: 未参与 Draw 的 dirty Material
- **WHEN** MaterialRenderProxy 被更新但本帧无可见 Primitive 使用
- **THEN** 系统 MUST NOT 为其强制创建 frame-local constants/binding

#### Scenario: 同一帧多个 Draw 复用 Material
- **WHEN** 两个可见 MeshBatch 使用相同 MaterialRenderProxy 且中间没有 parameter、texture generation 或 ShaderMap/layout 变化
- **THEN** Renderer MAY 复用同一当前帧 Material logical binding，command list MUST 保活其全部实际 RHI resources

### Requirement: Material 只填充五组 binding 中的 Material group
Forward Base Pass MUST 分别解析 Global、View、Pass、Material、Object 五个 logical Binding Group：View 使用 `ViewUniformShaderParameters`，Object 使用 `PrimitiveUniformShaderParameters`，Material 使用 `MaterialRenderProxy` 的物化结果。MaterialRenderProxy MUST NOT 写入 View/Object group，也不得感知 Vulkan physical set、D3D register/root mapping 或 backend descriptor 类型。

最终 Draw SHALL 通过现有 `RHIGraphicsBindings` 原子提供所需 logical groups；VulkanPortable v1 的 Global+View physical set 0 聚合和 D3D11/D3D12 native mapping继续由 RHI/backend 处理。

#### Scenario: Material group 缺失
- **WHEN** 当前 ShaderMap Program 要求 Material group 而 MaterialRenderProxy 无法生成完整 binding
- **THEN** 对应 MeshBatch MUST 被跳过并返回可诊断错误，其他合法 batch MAY 继续录制

### Requirement: Texture 引用切换安全
MaterialInstance GT state MUST 先持有新 `TextureRef`，再 enqueue Proxy texture update；`MaterialRenderProxy` 中的 `TextureResource` pointer 只允许 RT 解引用。旧 TextureRef release MUST 排在 Proxy update 后。Draw 解析出的实际 RHI texture view/resource MUST 由 Material binding 与 command list 强引用到 queue completion。

#### Scenario: 旧 Texture 最后引用释放
- **WHEN** texture setter 切换引用且旧 Texture 无其他 GT owner
- **THEN** proxy update MUST 排在旧 Texture release command 之前

### Requirement: Material binding cache 使用明确失效条件
scalar/vector value 改变、texture representation identity 改变、`TextureResource` RT-only binding generation 改变，或完整 ShaderMap/layout candidate 成功发布时，MaterialRenderProxy MUST 把 Material binding 标记为 dirty。同一 native texture/view 的内容更新若不改变 binding generation，MUST NOT 仅因内容 upload 强制重建 Material binding。

#### Scenario: Texture 内容更新但 view 不变
- **WHEN** Material 使用的 Texture 只更新 mip 内容且 active view identity/binding generation 不变
- **THEN** MaterialRenderProxy MUST 继续复用兼容 binding，command list 通过 resource state/strong ref 使用更新后的内容

### Requirement: 结构性变化走 replacement
blend mode、two-sided、depth policy、shading model、static switch、shader permutation、binding layout 或 vertex input requirement 变化 MUST NOT 走普通 setter，必须构建完整 ShaderMap/Proxy state candidate。只有 ShaderMap、binding layout、render state、所需 resources 与 pipeline/VertexFactory compatibility 都验证成功，且需要 GPU 工作时对应 business submit 成功后，candidate 才能发布；失败时 active state MUST 保持可用。

Material/ShaderMap SHALL 提供 `ShaderVertexInput`，由 `LocalVertexFactory` 完成匹配。Material 不得创建 vertex layout，VertexFactory 不得选择 Material、ShaderMap Program 或 permutation。

Shader frontend MUST 将 `.shader` Pass state 规范化为 `ShaderGraphicsPassState`，ShaderMapEntry v3 MUST 同时持久化该实际 state 与 `pass_template_hash`，reader/writer MUST 由 state 重算并严格校验 hash。`pass_template_hash` 只标识 Shader Pass template，不得替代 state，也不得包含 Material policy 或 attachment compatibility。

Material candidate MUST 在发布前从 Shader template state 派生完整 effective state。`two_sided == false` 时保留 Shader Pass 的 cull mode；`two_sided == true` 时 effective cull mode MUST 为 `None`，其余字段保持 Shader Pass 值。该派生只能发生在 candidate 构建/验证阶段，Base Pass 不得在 draw 时临时覆盖 cull state。PSO 创建与缓存 MUST 使用包含 effective state 的完整 pipeline descriptor，因此同一 Shader Pass 的单面与双面 Material 不得错误复用 pipeline。

#### Scenario: Static switch 修改
- **WHEN** MaterialInstance 修改 static switch
- **THEN** 当前 proxy MUST 保持可用，candidate 未完成前不得发布不完整 shader/binding state

#### Scenario: Candidate vertex input 不兼容
- **WHEN** candidate ShaderMap 的 ShaderVertexInput 与当前 StaticMesh LocalVertexFactory 不兼容
- **THEN** 对应 combination MUST 可诊断地不可绘制，不得用旧 vertex layout 拼接新 Shader 创建残缺 pipeline

#### Scenario: 双面 Material candidate
- **WHEN** `two_sided` 为 true 且 Shader Pass template 声明 Back culling
- **THEN** candidate MUST 保留原 `pass_template_hash` 以标识 Shader template，同时把 effective cull mode 固化为 `None`；Base Pass MUST 以该 effective state 创建或查询 PSO

#### Scenario: 双面切换 candidate 失败
- **WHEN** active Material 为单面且切换 `two_sided` 的 candidate 未完成完整验证或提交失败
- **THEN** active ShaderMap、effective state 与 binding MUST 全部保持单面旧值，不得只发布 `CullMode::None`

## Minimal Implementation Example

> Non-normative：本示例只说明推荐的 ownership、GT/RT 线程、调用顺序和失败方向；若与 Type Contracts 或 requirements 冲突，以规范性内容为准。

```text
GT creation:
MaterialInstance owns MaterialRef
→ create stable MaterialRenderProxy allocation
→ keep Proxy address as opaque RenderCommand identity

GT dynamic updates before Draw:
set scalar(parameter, value A)
→ validate ShaderParameterId/type
→ update GT override
→ enqueue proxy identity + owned value A
set scalar(parameter, value B)
→ enqueue proxy identity + owned value B

RT FIFO:
apply value A to MaterialRenderProxy parameter table
→ mark Material binding dirty
→ apply value B
→ retain only the final value for Draw materialization

Forward Base Pass for one visible MeshBatch:
ViewInfo provides ViewUniformShaderParameters
→ PrimitiveSceneInfo/Proxy provides PrimitiveUniformShaderParameters
→ MaterialRenderProxy resolves override/default values
→ read the active candidate ShaderGraphicsPassState
→ resolve TextureResource active views and binding generations
→ allocate frame-local Material constants only now
→ materialize Material logical binding
→ combine Global/View/Pass/Material/Object in RHIGraphicsBindings
→ bind and draw
→ command list keeps actual buffers/views/bindings alive to completion

Invisible Material:
keep dirty parameter state only;
do not allocate frame-local constants or descriptor work.

Texture setter:
establish new Texture Asset strong reference
→ enqueue MaterialRenderProxy texture update
→ release old Texture reference only after the update command

Failure A:
if ShaderParameterId/type validation fails, keep prior GT state and enqueue nothing.

Failure B:
if a required TextureResource is unavailable for the current recording,
skip the MeshBatch and diagnose; do not bind an incomplete Material group.

Failure C:
if structural candidate validation or submit fails,
retain the active ShaderMap/layout/render state and Material binding path.

Two-sided structural candidate:
copy the Shader Pass template state
→ if MaterialDesc::two_sided is true, set effective cull mode to None
→ validate the complete candidate and publish it atomically
→ keep pass_template_hash scoped to the unmodified Shader template
→ let the complete RHI pipeline descriptor distinguish the effective PSO.

Final release:
enqueue all StaticMeshSceneProxy material update/remove commands
→ move unique MaterialRenderProxy ownership into the last release command
→ RT destroys the Proxy after no Scene Proxy can reference it.
```

Batch B/C SHALL keep only focused setter FIFO/type-validation and one visible on-demand materialization smoke. Full MaterialInstance GT update→MaterialRenderProxy RT update→visible MeshBatch→five-group binding→Base Pass→submit/present coverage belongs to the concentrated final end-to-end test batch; this Spec MUST NOT require a separate large fixture for every parameter setter.
