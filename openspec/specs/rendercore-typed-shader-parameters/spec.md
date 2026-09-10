# rendercore-typed-shader-parameters Specification

## Purpose
定义由 Shader schema 驱动的强类型参数、不可变 metadata、canonical 编码和 logical BindingSet 创建 contract，使 RenderScene 调用方只填写领域字段而不直接管理 Shader identity、数据 ABI 或底层 RHI binding descriptor。

## Requirements

### Requirement: Shader schema 是参数身份与布局的唯一权威
每个可由 C++ 填写的 Global、View、Pass、Material 或 Object 参数集合 SHALL 具有一份完整 logical schema。`.shader` 中声明的 Pass/Material schema 以及引擎内建 Global/View/Object schema MUST 经过同一 ShaderCompiler 规则生成强类型 C++ parameters metadata；调用方不得另行手写 canonical name、member offset、`ShaderParameterId`、data layout hash 或 Shader ABI version。

生成 metadata MUST 包含 logical group、constant buffer identity 与 byte size、每个 constant member 的 identity/type/offset/size/array stride/matrix stride、每个 resource 的 identity/type/array count 以及完整 data layout identity。每个生成单元 MUST 只输出一个写入构建目录的 `.generated.h`，不得生成配套 `.cpp`；parameters struct、只读 metadata accessor 和薄 `inline` 编码重载 MUST 位于该头文件，内容和顺序 MUST 确定，且必须通过 schema/content identity 与对应 Shader artifact 建立严格一致性校验。

每个 generated header MUST 由 CMake 标记为 `GENERATED`、通过显式 `target_sources()` 加入实际消费 target，并归入 VS `Generated Files\\Shader Parameters` 工程分组。

#### Scenario: 同一 schema 重复生成
- **WHEN** 相同 `.shader` schema 和 Shader ABI version 在相同工具版本下重复生成 C++ parameters metadata
- **THEN** 生成内容、字段顺序、parameter identity 和 data layout identity MUST 完全一致

#### Scenario: generated metadata 与 Shader artifact 不一致
- **WHEN** runtime 发现强类型 parameters metadata 的 schema identity 与加载的 Shader artifact 不一致
- **THEN** logical binding 创建或 Program publication MUST 在 native binding 前可诊断失败，不得按字段名、声明顺序或大小近似兼容

### Requirement: 上层只填写强类型参数字段
RenderScene 和业务 Pass SHALL 通过具体的强类型 parameters 值表达 constant、texture、sampler 和 buffer 输入。正式调用点 MUST NOT 直接调用 parameter ID 生成函数、填写 Shader ABI/layout hash、构造 `RHIBindingValue`/`RHIBindingSetDesc`，也不得用字符串字典、无类型 variant 包或通用 builder 逐字段拼装参数。

强类型 parameters 中的空资源、错误资源类型、数组元素缺失和不支持字段 MUST 在 logical BindingSet 创建期间返回可诊断失败；不得静默省略 required value 或推迟到 backend downcast 才发现。

#### Scenario: Pass 填写合法参数
- **WHEN** Pass 填写其 generated parameters 的全部 required constant 和 resource 字段并创建 transient shader binding
- **THEN** RenderCore MUST 自动执行 canonical 序列化、附加 metadata identity 并返回对应 logical group 的 BindingSet，Pass 不得接触任何 native slot 或 ABI 字段

#### Scenario: Pass 遗漏 required texture
- **WHEN** 强类型 parameters 的 required texture 字段为空
- **THEN** binding 创建 MUST 返回可诊断失败且不得上传 constant data、创建半成品 logical set 或录制 native bind

### Requirement: 参数创建入口表达生命周期而非 Prepare 协议
RenderCore SHALL 为强类型 parameters 提供明确的 transient 与 persistent binding 创建语义。Pass-local parameters 可创建 recording-scoped transient binding；Global、View、Material 与可跨 mesh pass 复用的 Object binding MUST 由其领域 owner 按既有生命周期创建和缓存。系统 MUST NOT 引入统一 `.prepare()` 协议、`Prepared*` 对象、通用 Pass 基类、每 Pass binder/adapter、字符串/variant builder 或全局可变 metadata registry。

#### Scenario: Tonemap Pass 参数
- **WHEN** Tonemap 填写一次强类型 Pass parameters
- **THEN** 它 MUST 能通过一个明确的 transient binding 创建操作获得 Pass logical BindingSet，且不需要构造或持有额外 binder 对象

#### Scenario: BasePass 与 ShadowPass 使用同一 View
- **WHEN** 同一帧的 BasePass 和 ShadowPass 使用同一个 ViewInfo
- **THEN** 两个 Pass MUST 直接消费 View owner 已有的同一 logical BindingSet，不得各自执行 transient 创建或通用 prepare 回调

### Requirement: Canonical encoder 不依赖 C++ 对象布局
RenderCore SHALL 依据 generated metadata 逐字段读取强类型 parameters，并按 ToyShaderABI 写入零初始化的 canonical constant bytes；不得 raw-copy 完整 C++ struct representation。Matrix 方向、array/matrix stride、padding 和数值类型转换 MUST 以 metadata 为准，resource 字段 MUST 形成独立 logical values 而不写入 constant bytes。

#### Scenario: C++ padding 与 Shader padding 不同
- **WHEN** 强类型 parameters 的宿主 C++ 布局包含与 Shader canonical layout 不同的 padding
- **THEN** encoder MUST 仍生成与 metadata 完全一致的 constant bytes，未写区域保持为零

### Requirement: Transient uniform upload 只表达内存与生命周期
公共 transient uniform upload descriptor SHALL 只包含 source bytes 和诊断信息，返回 slice SHALL 只包含 buffer、offset 和 logical size。上传 API MUST NOT 接收或返回 `ShaderParameterId`、data layout hash、Shader ABI version、logical group 或 Program metadata；这些兼容性信息由 RenderCore 根据 parameters metadata 附加到 logical uniform binding 并由 Pipeline resolver 验证。

#### Scenario: 非 Shader 调用方上传 transient bytes
- **WHEN** 一个不理解 Shader ABI 的上层组件通过公共 context 上传合法 transient uniform bytes
- **THEN** RHI MUST 只按 backend alignment 复制和保活 bytes，不要求调用方伪造 Shader metadata

### Requirement: Generated metadata 不进入 backend contract
Shader parameters codegen、metadata 与 canonical encoder SHALL 位于 ShaderFormat/ShaderCompiler/RenderCore 边界。Vulkan、D3D11 和 D3D12 backend MUST 继续只消费已验证的 logical BindingSet 和当前 Pipeline active layout；generated C++ 类型、字段地址、Pass 类型或 Material schema 对象不得泄漏到公共 RHI backend hook。

#### Scenario: 新增 ShadowPass parameters
- **WHEN** 新增一个具有 Pass group constants 和 resources 的 ShadowPass Shader schema
- **THEN** 该 Pass MUST 能生成并使用强类型 parameters，而无需修改公共 RHI 接口、Vulkan physical-set 映射或新增 Pass 基类
