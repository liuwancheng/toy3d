# game-render-framework/static-mesh-resources Specification

## Purpose
定义 StaticMesh Asset 的 RenderData、各类 vertex/index buffer、VertexFactory、Shader vertex-input reflection、整体 ready gate 与 replacement 边界，使 Mesh 不会以部分可用状态参与 Draw，也不让任一 backend 的 native vertex-layout 语义泄漏到 RenderCore。

## Requirements

### Requirement: 子资源独立初始化
position、normal/UV、optional color 和 index buffer SHALL 作为独立 RenderResource 初始化。第一阶段 stream contract 固定为 `POSITION0=float3`、`NORMAL0=float3`、`TEXCOORD0=float2` 与 optional normalized `COLOR0`；`LocalVertexFactory` MUST 在 POSITION0、NORMAL0、TEXCOORD0 和 index buffer 的创建、upload/transition 录制及 layout validation 全部成功后，才允许该完整 candidate 参与当前 recording。

缺少 optional COLOR0 时，LocalVertexFactory SHALL 省略 COLOR0 component；不要求 COLOR0 的 Shader 可以匹配，要求 COLOR0 的 Shader MUST 判定不兼容。第一阶段不得通过 Vulkan-only constant attribute、D3D-only input-layout 特例或隐藏默认 buffer 伪造跨后端成功。

#### Scenario: 同帧初始化与 Draw
- **WHEN** 所有 Mesh 子资源在本帧录制 upload
- **THEN** upload、transition 和使用这些资源的 Draw MAY 在同一业务 list 内执行，无需等待 GPU

#### Scenario: Optional color 缺失
- **WHEN** StaticMesh 没有 ColorVertexBuffer 且 ShaderVertexInput 不要求 COLOR0
- **THEN** LocalVertexFactory MUST 以 POSITION0、NORMAL0、TEXCOORD0 完成匹配

#### Scenario: Shader 要求缺失的 color
- **WHEN** StaticMesh 没有 ColorVertexBuffer 而 ShaderVertexInput 要求 COLOR0
- **THEN** 对应 MeshBatch MUST 被判定不兼容并跳过，Renderer MUST 保留可诊断错误

### Requirement: VertexStreamComponent 只表达 logical stream mapping
每个 `VertexStreamComponent` MUST 明确保存 `ShaderVertexAttributeId`、stream index、byte offset、stride 和共享 `PixelFormat`，并引用同一 `StaticMeshRenderData` 保活的 buffer。公共 graphics pipeline vertex layout MUST 使用同一个 `PixelFormat`；backend 才将其映射为 `VkFormat` 或 `DXGI_FORMAT`。一个 LocalVertexFactory 内 logical attribute identity MUST 唯一，buffer range、stride、offset 与 format MUST 在参与 pipeline 前验证。

#### Scenario: 重复 POSITION0
- **WHEN** LocalVertexFactory 含两个 POSITION0 VertexStreamComponent
- **THEN** VertexFactory validation MUST 失败，不得选择其一继续创建 pipeline

### Requirement: ShaderMap 保留 vertex input metadata
Shader compiler 已持久化的 vertex-stage `ReflectedInterfaceVariable` MUST 由 `ShaderMapEntryLoader` 完整转换为 runtime `ShaderVertexInput`，并继续进入 `RHIShaderDesc` 的 `RHIShaderVertexInputReflection`。runtime MUST NOT 根据 Shader 名、源码约定、hard-coded location 或当前 LocalVertexFactory 猜测丢失的 input metadata。

Loader MUST 拒绝重复 logical attribute、unsupported scalar/component shape、缺失 target mapping、同一 Program 内冲突的 vertex input；非 vertex stage 的 interface input MUST NOT 被误当作 VertexFactory contract。

#### Scenario: Loader 读取 POSITION0
- **WHEN** vertex Shader reflection 含 semantic POSITION0、合法 location、float scalar 与三个 components
- **THEN** runtime ShaderVertexInput MUST 保存 POSITION0 logical identity 和数据形状，RHIShaderVertexInputReflection MUST 保留 semantic/index/location 供当前 backend 使用

#### Scenario: Runtime metadata 缺失
- **WHEN** vertex Shader bytecode 存在但所需 interface variable metadata 缺失或无法转换
- **THEN** ShaderMap program 创建 MUST 可诊断失败，不得推迟到 pipeline backend 猜测输入

### Requirement: LocalVertexFactory 匹配 ShaderVertexInput
`LocalVertexFactory` MUST 以 `ShaderVertexAttributeId` 和 scalar/component/format compatibility 匹配 `ShaderVertexInput`，输出完整 graphics pipeline vertex layout 和 draw-time vertex buffer bindings。VertexFactory MUST NOT 选择 Material、ShaderMap Program、Shader permutation、Binding Group 或 graphics render state。

第一阶段不实现 `VertexFactoryType` 全局注册、VertexFactory shader parameters、独立 VertexFactory permutation domain、`.shader VertexLayout`、manual vertex fetch 或 GPU Scene。

#### Scenario: POSITION0 format 不兼容
- **WHEN** ShaderVertexInput 要求 float3 POSITION0，而对应 VertexStreamComponent 不能提供兼容的三分量 float format
- **THEN** matching MUST 失败并阻止对应 pipeline/draw，禁止依赖 backend format coercion

### Requirement: RHI vertex reflection 可由三后端等价实现
`RHIShaderVertexInputReflection` MUST 同时携带 semantic name/index、location、scalar type 与 component count，且不得包含 `Vk*`、D3D input-layout object、root-signature 或其他 native 类型。Vulkan backend SHALL 使用 location 与 format 建立 native vertex-input state；D3D11/D3D12 backend SHALL 使用 semantic name/index、format 和 vertex bytecode建立 input layout/PSO。跨 target parity MUST 比较 logical attribute identity 与数据形状，不比较 native location、register 或 slot 数字。

公共 validation MUST 在进入 backend pipeline creation 前拒绝重复 location/semantic、unsupported format/shape 和 Shader input 与 pipeline vertex layout 不匹配。Vulkan ES3.1 profile 不依赖额外 feature；各 backend 不支持的 format MUST 返回 `Unsupported` 或等价可诊断失败，不得无操作成功。

#### Scenario: Vulkan 与 D3D native mapping 不同
- **WHEN** 同一 ShaderVertexInput 在 Vulkan 使用 location 0、在 D3D 使用 POSITION0 semantic
- **THEN** 两个 target MUST 以相同 logical attribute/data shape 通过 parity，且各 backend 只消费自己的 native mapping

### Requirement: 整体 ready gate
`StaticMeshRenderData` MUST 以完整 candidate 为单位控制可绘制性。当前 recording 只有在全部必要 buffer upload/transition 和 LocalVertexFactory validation 成功后，才 MAY 按同一 list 内的 upload-before-draw 顺序局部使用该 candidate；长期 `Ready` 只能在包含全部必要资源的 business list submit 成功后发布。任一必要子资源失败、frame abort 或 submit 失败时不得发布 Ready 或部分 Draw，CPU initial payload MUST 保留以供重录。

#### Scenario: Index buffer 失败
- **WHEN** vertex buffers 成功而 index buffer 初始化失败
- **THEN** Mesh MUST fallback 或 skip，并记录完整诊断

#### Scenario: Frame abort 后重录
- **WHEN** 全部 Mesh uploads 已录制但 frame 在 submit 前 abort
- **THEN** candidate MUST 保持 PendingUpload，不得成为长期 Ready；后续有效 frame MUST 能从保留的 initial payload 重录

### Requirement: 第一阶段只初始化当前单组 geometry
第一阶段 SHALL 一次初始化当前 StaticMesh Asset 的唯一 geometry/sections，不实现 LOD selection、streaming、partial residency 或 upload byte budget。未来资产模型增加 LOD 时 MUST 另行确认 render-data 子结构与选择 contract，不得把当前 sections 暗中解释为 LOD。

#### Scenario: 当前 StaticMesh 初始化
- **WHEN** StaticMeshRenderData 开始 init
- **THEN** 当前 Asset 的全部 vertices、indices 和 sections MUST 加入同一完整 candidate，不得只初始化部分 section

### Requirement: Replacement 使用完整候选 RenderData
重建 Mesh MUST 创建新的完整 `StaticMeshRenderData` candidate；只有 candidate 的必要 buffers、LocalVertexFactory、Shader-compatible layout 和 business submit 全部成功后，Proxy 才能按 FIFO 更新切换引用。旧 representation 的 Proxy update/remove MUST 排在 ownership-transfer release 之前；实际 RHI objects 继续由 in-flight command list/deferred deletion 保活到 queue completion。

#### Scenario: Candidate 失败
- **WHEN** 新 RenderData 任一必要资源失败
- **THEN** 当前 active RenderData MUST 保持可用且不得被提前释放
