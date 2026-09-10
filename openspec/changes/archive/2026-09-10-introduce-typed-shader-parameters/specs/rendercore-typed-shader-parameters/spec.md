## Purpose

定义由 Shader schema 驱动的强类型参数、不可变 metadata、canonical 编码和 logical BindingSet 创建 contract，使 RenderScene 调用方只填写领域字段而不直接管理 Shader identity、数据 ABI 或底层 RHI binding descriptor。

## ADDED Requirements

### Requirement: Shader schema 是参数身份与布局的唯一权威
每个可由 C++ 填写的 Global、View、Pass、Material 或 Object 参数集合 SHALL 具有一份完整 logical schema。`.shader` 中声明的 Pass/Material schema以及引擎内建 Global/View/Object schema MUST 经过同一 ShaderCompiler 规则生成强类型 C++ parameters metadata；调用方不得另行手写 canonical name、member offset、`ShaderParameterId`、data layout hash 或 Shader ABI version。

生成 metadata MUST包含 logical group、constant buffer identity与byte size、每个constant member的identity/type/offset/size/array stride/matrix stride、每个resource的identity/type/array count以及完整data layout identity。每个生成单元 MUST只输出一个写入构建目录的`.generated.h`，不得生成配套`.cpp`；parameters struct、只读metadata accessor和薄`inline`编码重载 MUST位于该头文件，内容和顺序 MUST确定，且必须通过 schema/content identity 与对应 Shader artifact 建立严格一致性校验。

每个generated header MUST由CMake标记为`GENERATED`、通过显式`target_sources()`加入实际消费target，并归入VS generated Shader Parameters工程分组。该构建集成 MUST保证全新生成工程中的依赖顺序正确，同时允许开发者在VS中浏览生成文件、跳转定义、定位编译错误并在Debug构建中调试编码调用链。

#### Scenario: 同一 schema 重复生成
- **WHEN** 相同 `.shader` schema 和 Shader ABI version 在相同工具版本下重复生成 C++ parameters metadata
- **THEN** 生成内容、字段顺序、parameter identity 和 data layout identity MUST 完全一致

#### Scenario: 生成 metadata 与 Shader artifact 不一致
- **WHEN** runtime 发现强类型 parameters metadata 的 schema identity 与加载的 Shader artifact 不一致
- **THEN** logical binding 创建或 Program publication MUST 在 native binding 前可诊断失败，不得按字段名、声明顺序或大小近似兼容

#### Scenario: Visual Studio 打开生成工程
- **WHEN** CMake为Visual Studio生成包含Shader parameters消费者的工程
- **THEN** 每个`.generated.h` MUST出现在约定的Generated Files工程分组中，并作为构建依赖在消费者编译前生成，不需要配套`.generated.cpp`

### Requirement: 上层只填写强类型参数字段
RenderScene 和业务 Pass SHALL通过具体的强类型 parameters 值表达 constant、texture、sampler和buffer输入。正式调用点 MUST NOT直接调用 parameter ID生成函数、填写 Shader ABI/layout hash、构造 `RHIBindingValue`/`RHIBindingSetDesc`，也不得用字符串字典、无类型variant包或通用builder逐字段拼装参数。

强类型 parameters 中的空资源、错误资源类型、数组元素缺失和不支持字段 MUST在 logical BindingSet 创建期间返回可诊断失败；不得静默省略 required value 或推迟到 backend downcast 才发现。

#### Scenario: Pass 填写合法参数
- **WHEN** Pass 填写其生成 parameters 的全部 required constant和resource字段并创建 transient shader binding
- **THEN** RenderCore MUST自动执行canonical序列化、附加metadata identity并返回对应logical group的 BindingSet，Pass不得接触任何native slot或ABI字段

#### Scenario: Pass 遗漏 required texture
- **WHEN** 强类型 parameters 的 required texture 字段为空
- **THEN** binding创建 MUST返回可诊断失败且不得上传constant data、创建半成品logical set或录制native bind

### Requirement: 参数创建入口表达生命周期而非 Prepare 协议
RenderCore SHALL为强类型 parameters提供明确的 transient 与 persistent binding创建语义。Pass-local parameters可创建 recording-scoped transient binding；Global、View、Material与可跨mesh pass复用的Object binding MUST由其领域owner按既有生命周期创建和缓存。系统 MUST NOT引入统一 `.prepare()` 协议、通用Pass基类、每Pass binder/adapter对象或要求所有五组经过相同生命周期入口。

#### Scenario: Tonemap Pass 参数
- **WHEN** Tonemap填写一次强类型 Pass parameters
- **THEN** 它 MUST能通过一个明确的transient binding创建操作获得Pass logical BindingSet，且不需要构造或持有额外binder对象

#### Scenario: BasePass 与 ShadowPass 使用同一 View
- **WHEN** 同一帧的 BasePass 和 ShadowPass 使用同一个 ViewInfo
- **THEN** 两个Pass MUST直接消费View owner已有的同一logical BindingSet，不得各自执行transient创建或通用prepare回调

### Requirement: Canonical encoder 不依赖 C++ 对象布局
RenderCore SHALL依据生成 metadata逐字段读取强类型 parameters，并按 ToyShaderABI 写入零初始化的canonical constant bytes；不得raw-copy完整C++ struct representation。Matrix方向、array/matrix stride、padding和数值类型转换 MUST以metadata为准，resource字段 MUST形成独立logical values而不写入constant bytes。

#### Scenario: C++ padding 与 Shader padding 不同
- **WHEN** 强类型 parameters 的宿主C++布局包含与Shader canonical layout不同的padding
- **THEN** encoder MUST仍生成与metadata完全一致的constant bytes，未写区域保持为零

### Requirement: Transient uniform upload 只表达内存与生命周期
公共 transient uniform upload descriptor SHALL只包含source bytes和诊断信息，返回slice SHALL只包含buffer、offset和logical size。上传API MUST NOT接收或返回 `ShaderParameterId`、data layout hash、Shader ABI version、logical group或Program metadata；这些兼容性信息由RenderCore根据parameters metadata附加到logical uniform binding并由Pipeline resolver验证。

#### Scenario: 非 Shader 调用方上传 transient bytes
- **WHEN** 一个不理解Shader ABI的上层组件通过公共context上传合法transient uniform bytes
- **THEN** RHI MUST只按backend alignment复制和保活bytes，不要求调用方伪造Shader metadata

### Requirement: Generated metadata 不进入 backend contract
Shader parameters codegen、metadata与canonical encoder SHALL位于ShaderFormat/ShaderCompiler/RenderCore边界。Vulkan、D3D11和D3D12 backend MUST继续只消费已验证的logical BindingSet和当前Pipeline active layout；生成C++类型、字段地址、Pass类型或Material schema对象不得泄漏到公共RHI backend hook。

#### Scenario: 新增 ShadowPass parameters
- **WHEN** 新增一个具有Pass group constants和resources的ShadowPass Shader schema
- **THEN** 该Pass MUST能生成并使用强类型parameters，而无需修改公共RHI接口、Vulkan physical-set映射或新增Pass基类
