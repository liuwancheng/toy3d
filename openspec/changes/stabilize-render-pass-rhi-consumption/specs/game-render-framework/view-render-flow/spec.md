## MODIFIED Requirements

### Requirement: Forward Base Pass 消费可见 MeshBatch
visibility 完成后，仍可见的 `StaticMeshSceneProxy` SHALL 为当前帧贡献 `MeshBatch`。Forward Base Pass MUST 逐 View 消费这些 batch，并明确分为 prepare 与 execute 两个连续阶段。

prepare MUST 匹配 `LocalVertexFactory` 与 `ShaderVertexInput`，从 Material active candidate 获取 effective `ShaderGraphicsPassState`，解析 Global、View、Pass、Material、Object bindings，并使用 Shader effective state、LocalVertexFactory vertex layout 与调用方提供的 color/depth attachment format、sample count 合成完整 `RHIGraphicsPipelineDesc`。prepare MAY 显式消费调用方提供的 `RHIDevice` 与当前 `RHIGraphicsCommandContext`，以创建或查询 shader、pipeline、binding，并在同一 recording 中先行录制 View/Object/Material constant upload；它 MUST NOT 创建或 finish command context、submit、present、wait 或开始 render pass。

prepare 的成功结果 MUST 只包含 execute 所需的 render-pass descriptor、viewport/scissor、pipeline、vertex/index bindings、graphics bindings 与 draw arguments，并强持有这些命令依赖的 RHI object。它 MUST NOT 保存或在 execute 时回读 `RHIDevice`、viewport、queue、frame context、RenderScene、Material、Proxy、MeshBatch 或其他可变准备源；结果只在当前帧当前 recording 中使用，不得成为跨帧 cache 或公开 pass 调度对象。

execute MUST 只以调用方提供的当前 `RHIGraphicsCommandContext&` 作为 RHI 行为入口，并只读取 prepare 已固化的数据来 begin/end render pass、设置 dynamic state 与 bindings、录制 draw。execute MUST NOT 创建 RHI object，不得重新解析 shader、material、visibility 或 mesh state，也不得创建/finish command list、submit、present、wait 或持有 viewport frame。prepare 与 execute MUST 使用同一个 recording context，并保持所有 prepare upload 严格先于消费它们的 draw。

Global/Pass group 未被 Program 声明时对应 binding set 保持 null；Program 声明了当前尚无 canonical parameter source 的 Global/Pass binding 时，当前 batch MUST 被诊断并跳过，不得伪造空 buffer 或无操作成功。

第一阶段尚未登记 per-draw dynamic-state override source。Base Pass MUST 在每个 draw 前显式设置 blend constants 为 white `(1, 1, 1, 1)`、stencil reference 为 `0`，使 `ConstantColor`/`OneMinusConstantColor` 与启用 Stencil 的合法 Shader state 在 Vulkan、D3D11 和 D3D12 具有确定且一致的默认值；不得依赖 backend 未初始化 state。未来开放 dynamic override 时 MUST 先登记其 ownership、thread 和 lifetime contract。

View/Object constant materialization MUST 根据 ShaderMap member identity、`ShaderValueType`、offset、size 与 matrix stride 逐字段写入 ABI byte buffer；不得 raw-copy `ViewUniformShaderParameters` 或 `PrimitiveUniformShaderParameters` 的 C++ object representation。Program 未使用某组时不创建对应 resource；未知 member、type mismatch、out-of-bounds 或暂不支持的 resource binding MUST 诊断并跳过当前 batch。

Vertex input 不兼容、`StaticMeshRenderData` 未通过整体可绘制 gate、其他必要 render representation 当前不可用、material/shader 无效或必要 binding 缺失时，prepare MUST 跳过对应 batch 并产生可诊断错误；不得创建残缺 pipeline、访问失效 resource 或无操作后报告成功。pass descriptor 或其他 pass-level input 无效时 prepare MUST 返回失败，execute MUST NOT 开始 render pass；execute 录制失败时 MUST 返回原始错误，由外层 frame owner discard 当前 recording 并调用 `abort_frame()`。

#### Scenario: 一个可见和一个被剔除的 StaticMesh
- **WHEN** Scene 中两个 StaticMeshSceneProxy 只有一个通过当前 ViewInfo 的视锥测试
- **THEN** Base Pass prepare MUST 只处理可见 Proxy 贡献的 MeshBatch，execute MUST 只录制由该 batch 形成的 draw

#### Scenario: VertexFactory 与 Shader 输入不兼容
- **WHEN** LocalVertexFactory 缺少 ShaderVertexInput 的必要 attribute
- **THEN** 对应 MeshBatch MUST 在 prepare 被跳过并记录可诊断错误，其他合法 batch MAY 继续准备和录制

#### Scenario: Program 声明暂不可提供的 Global 或 Pass binding
- **WHEN** ShaderMap Program 声明 Global 或 Pass group 的 active binding，但当前 Base Pass 没有已登记的 canonical parameter source
- **THEN** 对应 MeshBatch MUST 在 prepare 被跳过并产生可诊断错误，不得绑定空 set 后继续 draw

#### Scenario: Base Pass 录制边界
- **WHEN** 外层提供同一个 recording graphics context、合法 attachments 与可绘制 MeshBatch
- **THEN** prepare MUST 在 render pass 之前完成必要 RHI object 创建和 constant upload，execute MUST 只使用该 context 与已准备数据 begin render pass、录制全部合法 batch 并 end render pass

#### Scenario: 准备失败不进入 render pass
- **WHEN** Base Pass 的 pass-level descriptor validation 或必要准备步骤返回失败
- **THEN** execute MUST NOT 被调用，graphics context MUST 不收到 Base Pass 的 begin render pass 或 draw，外层 MUST discard recording 并闭合失败帧

#### Scenario: Execute 不使用 Device 创建入口
- **WHEN** Base Pass prepare 已成功且开始 execute
- **THEN** execute 期间 MUST 不调用任何 `RHIDevice::create_*()`、context 创建、queue submit、present 或 wait 路径
