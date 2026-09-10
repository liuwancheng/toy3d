## MODIFIED Requirements

### Requirement: init_views 创建完整 ViewInfo
`ForwardSceneRenderer::init_views()` MUST在RT为`SceneViewFamily`中每个`SceneView`创建一个`ViewInfo`，校验非空view rect、有效输出尺寸、正的near plane、投影模式以及全部必要矩阵值有限，并计算view、projection、view-projection及剔除所需的派生矩阵。每个`ViewInfo` MUST重置本帧可见结果并构造自己的`ConvexVolume`，不得复用上一帧的可见集合。

需要View logical Binding Group的业务pass录制前，Renderer MUST使用View强类型parameters metadata从已经验证的canonical ViewInfo values创建一次View logical BindingSet并由该ViewInfo持有；ViewInfo与Pass调用点 MUST NOT生成parameter ID、重述constant member layout、填写data layout hash/Shader ABI version或依赖具体Program active layout。`init_views()`仍不得让Shader parameters反向成为CPU矩阵和视锥校验的前置依赖。

`init_views()` MUST NOT拥有、查询或闭合`RHIViewportContext`/`RHIFrameContext`。它只返回CPU per-view初始化的可诊断结果；是否已经acquire frame以及失败后调用`abort_frame()`的责任属于外层Draw/frame orchestration。

#### Scenario: 多 View 初始化
- **WHEN** 一个SceneViewFamily含两个合法SceneView
- **THEN** `init_views()` MUST创建两个相互独立的ViewInfo、ConvexVolume和本帧可见结果，并在需要View binding时各自只创建一次logical BindingSet

#### Scenario: View 输入无效
- **WHEN** view rect为空、输出尺寸无效、near plane非正、矩阵包含非有限值或有效frustum plane退化
- **THEN** `init_views()` MUST返回可诊断失败，后续visibility和业务pass MUST NOT录制；若外层frame owner已经acquire frame，则外层 MUST通过`abort_frame()`闭合

### Requirement: Forward Base Pass 消费可见 MeshBatch
visibility完成后，仍可见的`StaticMeshSceneProxy` SHALL为当前帧贡献`MeshBatch`。独立BasePass模块 MUST逐View消费这些batch，匹配`LocalVertexFactory`与`ShaderVertexInput`，从Material active candidate获取effective `ShaderGraphicsPassState`，组合Global、View、Pass、Material、Object logical bindings，并将前向绘制录制到调用方提供的当前graphics context。

BasePass MUST使用Shader effective state、LocalVertexFactory vertex layout与调用方提供的color/depth attachment format、sample count合成完整`RHIGraphicsPipelineDesc`。它 SHALL自己begin/end对应render pass，但 MUST NOT创建/finish command list、submit、present、wait或持有viewport frame。Global/Pass group未被Program声明时对应binding set保持null；Program声明了当前尚无typed parameter source的Global/Pass binding时，当前batch MUST被诊断并跳过。

BasePass调用点 MUST NOT直接构造`RHIBindingValue`/`RHIBindingSetDesc`、调用parameter ID生成函数、计算constant layout hash或填写Shader ABI version。View、Material与Object binding MUST由各自owner或frame-local draw data提供；Pass-local数据若存在，必须由对应强类型Pass parameters通过transient binding创建入口产生。

第一阶段尚未登记per-draw dynamic-state override source。BasePass MUST在每个draw前显式设置blend constants为white `(1, 1, 1, 1)`、stencil reference为`0`，不得依赖backend未初始化state。

Vertex input不兼容、`StaticMeshRenderData`未通过整体可绘制gate、其他必要render representation当前不可用、material/shader无效或必要binding缺失时，Renderer MUST跳过对应batch并产生可诊断错误。

#### Scenario: 一个可见和一个被剔除的 StaticMesh
- **WHEN** Scene中两个StaticMeshSceneProxy只有一个通过当前ViewInfo的视锥测试
- **THEN** BasePass MUST只处理可见Proxy贡献的MeshBatch

#### Scenario: VertexFactory 与 Shader 输入不兼容
- **WHEN** LocalVertexFactory缺少ShaderVertexInput的必要attribute
- **THEN** 对应MeshBatch MUST被跳过并记录可诊断错误，其他合法batch MAY继续录制

#### Scenario: Program 声明暂不可提供的 Global 或 Pass binding
- **WHEN** ShaderMap Program声明Global或Pass group的active binding，但当前BasePass没有已登记的typed parameter source
- **THEN** 对应MeshBatch MUST被跳过并产生可诊断错误，不得绑定空set后继续draw

#### Scenario: typed binding 创建失败
- **WHEN** View、Object、Material或Pass强类型参数缺少required resource或metadata与Shader artifact不兼容
- **THEN** 对应MeshBatch MUST在begin render pass或native draw前被诊断并跳过，不得由BasePass手工补造底层binding value

#### Scenario: Base Pass 录制边界
- **WHEN** 外层已提供graphics context与合法color/depth attachments
- **THEN** BasePass MUST在begin前取得所有owner-provided bindings，随后begin、录制全部合法batch并end render pass，但 MUST NOT finish、submit、present或wait

## ADDED Requirements

### Requirement: Object binding 在 frame-local draw data 中复用
Renderer SHALL为当前frame中唯一的Primitive/Object canonical values创建至多一个Object logical BindingSet，并将其关联到frame-local mesh draw data。BasePass、ShadowPass和其他mesh pass消费相同Primitive values时 MUST复用该binding；visibility可以组织frame-local关联，但 MUST NOT实现Shader metadata编码、持有跨帧Object binding cache或成为新的资源系统。

#### Scenario: 同一 Primitive 进入 BasePass 和 ShadowPass
- **WHEN** 一个可见Primitive在同一frame同时贡献BasePass和ShadowPass MeshBatch且object transform未变
- **THEN** 两个mesh pass MUST复用同一Object uniform slice和logical BindingSet，不得分别上传或创建
