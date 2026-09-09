## MODIFIED Requirements

### Requirement: Forward Base Pass 消费可见 MeshBatch
visibility 完成后，仍可见的 `StaticMeshSceneProxy` SHALL 为当前帧贡献 `MeshBatch`。独立 BasePass 模块 MUST逐 View消费这些batch，匹配`LocalVertexFactory`与`ShaderVertexInput`，从Material active candidate获取effective `ShaderGraphicsPassState`，选择完整graphics Pipeline，并将Global、View、Pass、Material、Object五组Program-independent logical bindings组合进draw command后录制到调用方提供的当前graphics context。

Base Pass MUST使用Shader effective state、LocalVertexFactory vertex layout与调用方提供的color/depth attachment format、sample count合成完整`RHIGraphicsPipelineDesc`。它 SHALL自己begin/end对应render pass，但 MUST NOT创建/finish command list、submit、present、wait或持有viewport frame。Global/Pass group未被Program声明时对应binding set保持null；Program声明了当前尚无canonical parameter source的Global/Pass binding时，当前batch MUST被诊断并跳过，不得伪造空buffer或无操作成功。

第一阶段尚未登记per-draw dynamic-state override source。Base Pass MUST在每个draw前显式设置blend constants为white `(1, 1, 1, 1)`、stencil reference为`0`，使`ConstantColor`/`OneMinusConstantColor`与启用Stencil的合法Shader state在Vulkan、D3D11和D3D12具有确定且一致的默认值；不得依赖backend未初始化state。未来开放dynamic override时 MUST先登记其ownership、线程和生命周期contract。

View/Object constant bytes MUST根据canonical ShaderMap member identity、`ShaderValueType`、offset、size与matrix stride逐字段写入，不得raw-copy C++ object representation。一个View在当前frame MUST只生成一次canonical bytes、一次transient uniform slice和一个logical View BindingSet；Object MUST使用recording-scoped uniform slice而不是按draw创建独立长期GPU buffer。View、Material、Object owner MUST NOT接收或缓存当前Program binding layout或target slot。Program未使用某组时不创建对应资源；未知成员、类型不匹配、越界、constant data layout不兼容或暂不支持的资源类binding MUST诊断并跳过当前batch。

Vertex input不兼容、`StaticMeshRenderData`未通过整体可绘制gate、其他必要render representation当前不可用、material/shader无效或必要binding缺失时，Renderer MUST跳过对应batch并产生可诊断错误；不得创建残缺pipeline、访问失效资源或无操作后报告成功。

#### Scenario: 一个可见和一个被剔除的 StaticMesh
- **WHEN** Scene中两个StaticMeshSceneProxy只有一个通过当前ViewInfo的视锥测试
- **THEN** BasePass MUST只处理可见Proxy贡献的MeshBatch

#### Scenario: VertexFactory 与 Shader 输入不兼容
- **WHEN** LocalVertexFactory缺少ShaderVertexInput的必要attribute
- **THEN** 对应MeshBatch MUST被跳过并记录可诊断错误，其他合法batch MAY继续录制

#### Scenario: Program 声明暂不可提供的 Global 或 Pass binding
- **WHEN** ShaderMap Program声明Global或Pass group的active binding，但当前BasePass没有已登记的canonical parameter source
- **THEN** 对应MeshBatch MUST被跳过并产生可诊断错误，不得绑定空set后继续draw

#### Scenario: Base Pass 录制边界
- **WHEN** 外层已提供graphics context与合法color/depth attachments
- **THEN** BasePass MUST在begin前完成全部uniform slice和logical binding准备，再begin、录制全部合法batch并end render pass，但 MUST NOT finish、submit、present或wait

#### Scenario: 同一 View 被多个 Program 使用
- **WHEN** 同一View的两个MeshBatch使用不同Program target mapping但要求相同View constant ABI
- **THEN** 两个draw MUST复用同一个View logical BindingSet和uniform slice，不得创建Program layout adapter或重复上传View matrices

