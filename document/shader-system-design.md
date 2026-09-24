# Toy3d Shader 系统设计

## 1. 文档目的

本文定义 Toy3d Shader 系统的长期架构、Shader 资产语法、跨图形 API 编译链、参数布局、Binding、缓存、调试、运行时加载以及与后续材质系统和 RDG 的边界，并在文档末尾给出分阶段实现步骤。

目标图形后端为 Vulkan、Direct3D 11 和 Direct3D 12。D3D11 基线固定为 Feature Level 11_0、Shader Model 5.0、FXC/`D3DCompile` 与 DXBC，不支持 D3D10、Feature Level 10.x 或 Shader Model 4。开发者只维护 HLSL 和 Toy3d Shader 资产描述，不维护 GLSL、SPIR-V、DXBC 或 DXIL 源文件。发布运行时只消费离线编译产物，Editor/Development 构建可以按配置从源码重新编译。

本文中的产品边界已经确认：

- 使用类似 Unity ShaderLab、但范围更小的 `.shader` 容器；
- `Properties` 自动生成 Material 参数布局和 HLSL 声明；`Parameters` 为非Material的受控数值参数提供同一ToyShaderABI路径；
- `Pass` 可以声明跨 API 的 raster、depth/stencil、blend 等 PSO 模板状态；
- attachment、load/store、transition、资源依赖和执行顺序仍属于 renderscene/RDG；
- `Global`、`View`、`Pass`、`Material`、`Object` 是稳定的逻辑 Binding Group；
- `b/t/s/u` 是每个 Shader Program 确定性、紧凑分配的物理 slot，不是全局固定 ABI；
- ShaderCompiler 在编译前生成 target-specific physical mapping，编译后使用 reflection 验证逻辑身份、类型、布局和 stage visibility；不同 target 的 native slot 数字不要求相同；
- constant buffer 使用 Toy3d 自己定义的 HLSL ABI，不把 `std140` 或 `std430` 暴露为公共规则；
- 公开 Shader ABI 固定使用 32-bit 类型；局部计算可直接使用标准 HLSL `half/halfN`，但不保证最终一定采用 native FP16；
- Shader 支持预编译、源码重编译和 RenderDoc 源码级调试。

### 1.1 已锁定设计基线

本节是后续实现和验收的规范性基线。后文若仍有历史描述与本节冲突，以本节为准，并应在实施相关模块前清除冲突，不得选择性实现。

#### 平台、profile 与工具链

- 正式 target 为 Vulkan、D3D11、D3D12。D3D11 使用 FXC/`D3DCompile` 生成 SM5 DXBC；D3D12 使用 DXC 生成 SM6 DXIL；Vulkan 使用 DXC 生成 SPIR-V。
- 默认 `Vulkan ES3.1 profile` 固定为 Vulkan 1.1 与 SPIR-V 1.3，不默认依赖可选 device feature。Cook 必须按 profile 的最低能力和 limits 验证，不能根据开发机 GPU 自动提高要求；runtime 加载时再次复核 package 的 required capabilities/limits。
- 上层只通过 capability、limits、format support 与版本化 profile 选择功能路径，禁止散布 `if Vulkan`、`if Android` 等判断。所有新 RHI、Shader、资源格式、Binding 和 Pipeline 设计必须评估移动端。
- 编译服务采用统一 compile request 与 target-specific compiler adapter，不使用 HLSLCC、ShaderConductor 或运行时跨编译链。
- Toy3d 自建 DXC、SPIR-V Tools、SPIRV-Reflect 与 Toy3dShaderCompiler；`d3dcompiler_47.dll` 和 DXIL validator 使用锁定的 Microsoft 官方二进制。两类内容共同组成带版本、构建参数、SHA-256、license 与 compiler identity manifest 的 `Toy3dShaderToolchain` bundle。
- 本地和 CI 只使用 Toy3d 验证过的 bundle，不搜索 UE 安装目录、`PATH` 或未知系统版本。普通 Editor 用户无需安装完整 SDK；显式 bootstrap 下载预构建 bundle，CMake configure 不隐式联网；Shipping 不携带 compiler。

#### 语言、资产身份与 include

- `.shader Version 1` 是 language major。后续 v1 revision 可以增加不改变旧源码语义的可选关键词或类型；旧 compiler 遇到新功能报 `UnsupportedLanguageFeature`。未知关键词始终报错。改变旧语义、packing 或默认行为时升级 language/ABI major。
- `Version` 必须是 Shader block 第一项；之后顶层 section 顺序无关。`Properties`、`Parameters`、`Resources`、`Variants` 各最多一次，`HLSLINCLUDE` 与 `Pass` 可多次，至少一个 `Pass`；Pass 源码顺序保留。
- `Shader "Toy3d/..."` 的逻辑名是公开身份，文件虚拟路径只用于 include、cache 和 diagnostics。逻辑名由 `/` 分隔的 ASCII `[A-Za-z_][A-Za-z0-9_]*` 段组成，大小写敏感；Cook 拒绝完全重复和仅大小写不同的冲突。
- Pass 名称非空、在 Shader 内唯一且大小写敏感。稳定 `ShaderPassId` 不受 Pass 重排影响；重命名视为删除旧 Pass 并新增 Pass。
- Properties、Parameters、Resources、engine schema 与 Variants 生成的 HLSL 名称位于同一冲突域；`toy3d_`/`TOY3D_` 前缀保留。冲突直接报错，不自动改名。
- include 只接受规范化虚拟绝对路径。v1 允许 `/Engine/ShaderIncludes/`，未来允许 `/Project/ShaderIncludes/`；禁止本机绝对路径、相对路径、`..`、反斜杠、系统/CWD fallback。`/Generated/` 只能由 compiler 注入，用户不得直接 include。
- compiler 依次注入 `/Generated/ToyShaderPrelude.hlsli`、`/Generated/ToyBindings.hlsli`、Shader `HLSLINCLUDE` 和 Pass `HLSLPROGRAM`。generated 内容进入 compile key；Editor/Debug 可落盘 shadow copy，并用 `#line` 映射虚拟路径。
- include cycle 报完整链；最大深度默认 64，可配置降低；依赖图保存虚拟路径与 SHA-256。

#### 参数身份、确定性排序与 ToyShaderABI

- `ShaderParameterId` 使用稳定 64-bit FNV-1a，输入是带长度字段编码的 `binding_group + category + parameter_name`。identifier 为 ASCII、大小写敏感，0 保留为 invalid；类型不进入 ID。package 保存原始名称并在 Cook 检测 collision。
- 同一逻辑参数跨 Shader 得到同一 ID，但使用时还必须校验 layout hash。参数重命名视为删除旧参数并新增参数；MaterialInstance 的旧 override 保留为 orphan 并报告，未来只通过显式 `Alias`/`FormerlyNamed` 迁移，不做模糊匹配。
- 文档和实现统一使用“确定性排序规则”。constant-buffer member 严格按 schema/source 顺序 packing，重排会改变 layout hash；独立资源按 group、resource class、ID 确定性排序，源码重排不改变 native binding。
- 同一 Binding Group 的普通数值参数自动合并为一个 constant buffer，绝不跨 Global、View、Pass、Material、Object 合并。每 group 最大 16 KiB，超限报 `ConstantBufferSizeLimitExceeded`，v1 不自动分页。
- packing 采用 HLSL/D3D 16-byte register 规则：`float3 + float` 可共享 register，成员不得跨 16-byte 边界，数组元素 stride 至少 16 bytes，矩阵每列占一个 16-byte register，buffer 总大小向上对齐到 16 bytes，所有 padding 清零。
- CPU/GPU 权威格式是 byte buffer 加 layout metadata，禁止直接 `memcpy` 任意 C++ struct。内置 C++ 类型以后通过生成代码或 `static_assert` 验证。backend upload alignment（例如 D3D12 256 bytes）不改变成员 ABI。
- ToyShaderABI 标量为 Float32、Int32、UInt32；逻辑 Bool 使用 UInt32，false=0、true=1。内部 ABI 支持固定数组和 2x2 至 4x4 column-major matrix；`.shader Properties` v1 不开放数组，只公开 Matrix4x4。`.shader Parameters` 首阶段只开放Pass group的Float、Float2、Float3、Float4与Float4x4，不开放数组或struct。
- Vulkan 使用 DX-compatible relaxed cbuffer layout、显式 offset、reflection 与 `spirv-val --target-env vulkan1.1` 三重验证。reflection 是验证与 native mapping 来源，不是逻辑 schema 权威；binary 中出现 schema 未登记资源是错误。

#### 资源、Sampler 与 StructuredBuffer

- `Resources` 使用受控类型系统，不接受任意 HLSL type text。v1 支持 `Texture2D/2DArray/3D/Cube/2DMS`、`Sampler`、`ComparisonSampler`、`Buffer`、`ByteAddressBuffer`、`StructuredBuffer` 及对应已列入 EBNF 的 RW 类型。typed Texture/Buffer 元素只允许有 target format support 的 32-bit scalar/vector；StructuredBuffer 额外允许 Toy ABI matrix。`Texture2DMS` 只用 `Load`，不绑定 Sampler。
- v1 不支持任意 user-defined HLSL struct、descriptor array、bindless、Append/Consume、Texture1D 或 CubeArray。一个 Buffer 内有多个元素不等于 descriptor array。任意结构体等待 Toy `Struct` schema。
- StructuredBuffer stride 跨三 target 固定：scalar=4、vec2=8、vec3=16、vec4=16、matrix 每列 16 bytes；Float3 padding 清零，stride 进入 schema/reflection/layout hash。骨骼 palette 推荐共享 `StructuredBuffer<Float4>` 加 Object `bone_base_index`。
- Texture 与 Sampler 独立声明；compiler 不为 Texture 隐式生成 Sampler。具名 Sampler 可被多张 Texture 共享，HLSL 使用标准 `texture.Sample(sampler, uv)`。
- Sampler 是动态 Material/MaterialInstance 参数，修改 sampler 不重编 Shader；相同 `RHISamplerDesc` 由 device cache 去重。v1 preset 为 PointClamp、PointWrap、LinearClamp、LinearWrap、TrilinearClamp、TrilinearWrap、ShadowCompareClamp；后者 compare 为 reversed-Z 对应的 `GreaterEqual`。

#### Binding、active layout 与移动端限制

- 五个逻辑 Binding Group 固定为 Global、View、Pass、Material、Object；逻辑 group 表示身份、所有权和更新频率，不等于 descriptor set 或寄存器空间。
- 布局分为 logical schema、active Program layout、target-specific native mapping。完整 schema 保留全部参数/default；Program 只给实际使用的独立资源分配 binding。一个 group 的 cbuffer 只要有任一成员使用就保留完整 buffer 和稳定 offset，整个 group 未使用才不占 binding。
- D3D11/D3D12 按 stage、register class、group 固定顺序、`ShaderParameterId` 从 0 紧凑分配。Vulkan 独立生成 set/binding。
- `Vulkan ES3.1 profile` 使用四个 physical sets：set 0=Global+View，set 1=Pass，set 2=Material，set 3=Object。每个 set 内按 logical group、descriptor type、`ShaderParameterId` 从 0 连续分配；禁止 `0/256/512/768` class-base 预留。
- package 保存 target-specific mapping。跨 target reflection parity 比较逻辑身份、类型、array count、constant offset/stride 与 stage visibility，不比较 native slot/set/binding 数字。
- Cook 同时验证 per-stage 和 pipeline-layout 的 sampler、sampled image、uniform buffer、storage resource limits，并在错误中报告 group、stage、resource class、required 与 supported。

#### 数学、坐标、精度与 Shader interface

- 世界坐标为 left-handed：+X right、+Y up、+Z forward，Camera local forward 为 +Z，`1 Toy3d unit = 1 meter`。CPU 后续以 Toy3d math API 封装 GLM，禁止上层传播第三方矩阵约定。
- 使用 column-vector、column-major storage，HLSL 固定 `mul(matrix, vector)`，generated HLSL 显式 `column_major`。
- clip space 使用 D3D-style X/Y `-1..1`、depth `0..1`，全引擎 reversed-Z：near=1、far=0、depth clear=0.0、默认 DepthTest=`GreaterEqual`。公共 FrontFace 为 CounterClockwise。
- Vulkan 1.1 backend 使用 negative viewport height 处理 Y，并修正 native front-face mapping；Shader 不写 Vulkan Y flip 或 backend 分支。
- FBX、glTF、OBJ 等在 Import/Cook 边界转换到 Toy3d canonical space，覆盖 position、normal、tangent handedness、winding、node、skeleton、inverse bind、animation、camera、light 与 unit；转换规则版本进入 asset Cook key。
- 不提供 `toy_half` 或全局 half/full precision 开关。作者可对局部计算直接使用 HLSL `half/halfN`；compiler/profile/driver 可提升为 FP32，缺少 native FP16 不使 Shader Unsupported。
- 公开参数、cbuffer、portable vertex input、VS→PS varying 与公开 buffer schema 均使用 32-bit。Vulkan varying 不得用 `RelaxedPrecision` 破坏此保证，D3D reflection component type 必须为 Float32。
- VBO 可使用 Half、SNORM、UNORM 等物理格式节省显存，由 vertex fetch 转换为 HLSL `floatN`。HLSL vertex entry input signature 是唯一逻辑 schema，不新增 `.shader VertexLayout`。
- semantic 名称按 HLSL 规则大小写不敏感并规范化为 uppercase；`semantic + index` 生成稳定 `ShaderVertexAttributeId`。允许自定义 ASCII semantic，`SV_*` 保留；RenderCore 映射后才填写 RHI native location/semantic。

#### Pass、Variant 与 capability

- Program kind 由 pragma 推断。Graphics 必须且只能有一个 vertex entry，pixel entry 可选且不得有 compute；Compute 必须且只能有一个 compute entry，不得有 vertex/pixel 或 graphics state。
- normalized Pass 默认状态为 TriangleList、Cull Back、FrontFace CounterClockwise、Fill Solid、DepthTest GreaterEqual、DepthWrite On、Stencil Off、Blend Off、ColorWrite RGBA。省略与显式默认产生相同 Pass template hash；重复 state 报错。
- v1 不开放 indexed color attachment blend。Blend/ColorWrite 统一应用全部 active color attachments；`Blend` 只能是 Off 或完整 block。Stencil 支持 masks 0..255，以及 FrontAndBack 或成对 Front/Back。stencil reference 和 blend constants 为 dynamic command state。Blend factor 只保留 Vulkan、D3D11 与 D3D12 可无损映射的公共集合；v1 不提供 `ConstantAlpha`/`OneMinusConstantAlpha`，alpha 方程通过 `ConstantColor` 的 alpha 分量使用 blend constant。`Fill Wireframe` 属于可持久化的跨 API 状态，但 target/profile 缺少 non-solid fill capability 时必须明确裁剪或返回 `Unsupported`。
- Pass pipeline state 不进入 Shader permutation key，只进入 Pass template hash 与 PSO key；attachment count/format/sample/load/store 仍由 RDG/renderscene 提供。
- Variant 和 enum value 有稳定 ID；permutation key 按 VariantId 确定性序列化。默认 soft limit 为 256/Pass、hard limit 为 4096/Pass，项目可配置；先做 capability culling，再计数，soft warning、hard error并检测乘积溢出。
- 可选高级路径必须显式 `Requires <Capability>`。不支持该 requirement 的 target/profile 正常裁剪；使用受限能力却未声明时 Cook 报 `MissingCapabilityRequirement`；runtime 不得请求 requirements 不满足的 Pass。

#### Hash、package、Editor 与故障处理

Shader 数据沿用容易识别的 UE 风格术语，但职责以 Toy3d 本文定义为准：

- `ShaderCompilerOutput`：目标编译器刚产生、尚未进入持久化索引的内存结果，包含 binary、diagnostics 和原始 reflection；
- `ShaderCodeEntry`：单个 vertex、pixel 或 compute stage 经 validation/parity 后的稳定记录；
- `ShaderMapEntry`：一个 Shader、Pass、permutation、target/profile 组合的完整记录，拥有各 stage `ShaderCodeEntry`、合并 reflection、Binding Map 和 Pass template identity；
- `ShaderMap`：Editor/runtime 按 Shader、Pass、permutation 和 target/profile 查询 `ShaderMapEntry` 的索引与生命周期容器；
- `ShaderCodeLibrary`：Cook 后按平台发布、按 content hash 去重的只读 Shader code 集合。

运行时加载和索引使用以下固定术语：

- `ShaderMapLoader`：按完整 `ShaderMapProgramKey` 读取一个 `ShaderMapProgramData` 的来源无关接口；它只负责加载，不拥有运行时缓存；
- `ShaderMapEntryLoader`：读取独立、已验证 `ShaderMapEntry` 的开发加载实现，不以 Editor、Debug 或 loose 等宿主/配置术语命名；
- `ShaderCodeLibraryLoader`：读取 Cook 后 `ShaderCodeLibrary` 的发布加载实现；
- `ShaderMap`：Toy3d 进程级、跨 Shader 类型的已加载 Program 索引与生命周期容器。它接近 UE `TShaderMap` 的查询和持有职责，但不等同于有明确领域作用域的 `FGlobalShaderMap` 或 `FMaterialShaderMap`；
- `ShaderMapProgramData`：Loader 交付、尚未进入 `ShaderMap` 的 CPU 数据，包含 bytecode、reflection、binding metadata 与稳定 hash；
- `ShaderMapProgram`：经 `ShaderMap` 公共语义验证并建立参数索引后的不可变 CPU 对象；
- `RHIShaderProgram`：由 Render Thread 基于 `ShaderMapProgram` 创建的 RHI Shader 与 Binding Layout 组合，不由 `ShaderMap` 持有。

`ShaderMapProgramKey` 是精确逻辑查询身份，由 Shader 名、Pass 名、`ShaderPlatform` 与必选 permutation key 组成。`ShaderPlatform` 对应 UE `EShaderPlatform` 的目标能力概念，当前取值为 `VulkanES31`、`D3D11SM5` 与 `D3D12SM6`；公开查询不再同时接收可形成非法组合的 binary format/profile。具体 binary format 由 `ShaderPlatform` 确定。当前不引入额外 `ShaderMapProgramId` 或 generation 索引；Program 内容 hash 保存在 `ShaderMapProgramData` 中，待 DDC 或热重载需要精确编译身份时再形成独立类型。

Loader 负责来源格式、边界、版本、manifest/hash 与索引验证；`ShaderMap` 是运行时公共语义的唯一守门人，负责验证请求身份、stage 组合、binding/reflection 与 constant layout。只有成功 Program 进入缓存，失败不做 negative cache。Program 发布后不可原地修改；未来热重载创建新对象并替换 current 引用，失败保留 last-known-good，旧对象仅因 Game/Render Thread 或 RHI/PSO 引用继续存活。

当前单线程 bootstrap 允许 renderscene 临时同步调用 `ShaderMap::find_or_load()`。正式 Game Thread/Render Thread 分离后，由 Game Thread 驱动 `ShaderMapLoader` 和 CPU Program 加载，向 Render Thread 发布不可变 `ShaderMapProgramRef`；Render Thread 不执行 Shader 文件 I/O，只创建和持有 `RHIShaderProgram`/PSO。该线程迁移是未来方向，本批次不提前引入线程框架。

`Artifact`、`LooseArtifact` 和 `ProgramArtifact` 不再作为 Shader 领域类型名；`Program` 仅描述 graphics/compute stage 组合语义，不代替 `ShaderMapEntry` 的持久化身份。

- 分离 `parameter_schema_hash`、`logical_layout_hash`、`target_binding_hash`、`bytecode_content_hash`。默认值、UI、sampler preset 进入 schema hash但不进入 logical layout；native mapping 只进入 target binding hash。compile/package/dependency/bytecode 内容寻址统一 SHA-256，`ShaderParameterId` 仍使用 FNV-1a。
- ShaderPackage 使用 little-endian、定宽字段、section table、major/minor、required/optional section；独立记录 ABI、ID algorithm 与 target mapping version，不 dump C++ struct。reader 验证范围、重叠与整数溢出。格式支持 multi-target，Cook 按产品裁剪并用显式 profile/fallback chain 选择；v1 不压缩。
- Editor/uncooked 不加载 ShaderCodeLibrary，而是读取以 ShaderMap key 分目录的已验证 `ShaderMapEntry`：manifest、`ShaderCodeEntry`、reflection、dependencies、debug。Cook 收集、验证、去重后生成只读 ShaderCodeLibrary；Shipping 只加载 library。
- Cook/Shipping ShaderCodeLibrary 优先 memory map，不支持时回退普通只读 buffer并按需解析；热更新写入新 content-hash 文件，不能覆盖仍在使用的映射文件。
- Debug 使用低优化与 embedded debug并保留源码；Development 正常优化并使用外置 symbols；Shipping strip 源码、symbols 与绝对路径。debug mode 进入 compile key，不进入 logical layout hash。
- 热重载在后台编译和验证。有 last-known-good 时失败继续使用旧 Shader；首次失败使用 pass-family 兼容 Error Shader。Compute 无通用 fallback，除非功能显式提供，否则跳过并诊断；Global/copy/present 等核心 Shader 失败不能伪装成功。
- layout 变化按 `ShaderParameterId + type` 迁移；类型不一致使用新默认并 warning，旧 override 成为 orphan。新 Program/PSO 全部成功后在 frame safe point 原子切换，旧 RHI 对象按 queue completion 延迟释放；批次任一 PSO 失败则整批回滚。

#### Diagnostics 与 parser recovery

- `max_error_count` 与 `max_warning_count` 默认均为 100，CLI 暴露 `--max-errors`、`--max-warnings`；0 表示无用户上限，但仍受内部安全上限约束。达到上限追加 `DiagnosticLimitReached`。
- 任一 error 都禁止生成 compile request 或 package。parser 在 top-level、section、Pass state 与 HLSL block 使用明确恢复点；v1 CLI 不向后续阶段暴露 partial AST。
- 必须提供可定位的错误码，至少包括 `UnsupportedLanguageFeature`、`ShaderParameterIdCollision`、`VariantIdCollision`、`InvalidVariantSelection`、`ConstantBufferSizeLimitExceeded`、`DuplicatePass`、`DuplicatePassState`、`MissingCapabilityRequirement`、`ShaderInterfacePrecisionMismatch`、`ReflectionUnexpectedResource` 与 `DiagnosticLimitReached`。
- `Toy3dShaderCompilerCore` 只返回结构化 diagnostics，不直接依赖日志系统。CLI 使用统一 formatter 将 diagnostics 写入 `stderr`，同时通过 `Toy3dLogging` 留存在 `bin/saved/logs/shader_compiler.log`；运行时日志位于同目录的 `toy3d.log`。日志文件默认按 10 MiB、最多 5 个文件轮转。

## 2. 目标与非目标

### 2.1 目标

- 一份 Shader 源码自动生成 Vulkan、D3D11、D3D12 的目标字节码；
- 自动生成、序列化和验证参数布局与 Binding Layout；
- 不在 renderscene、材质或 pass 代码中硬编码物理 slot；
- 支持多个 entry point、graphics/compute program、permutation 和 capability 裁剪；
- 支持 Debug、Development、Shipping 三种 Shader 调试与优化策略；
- 支持 include 依赖追踪、增量编译、热重载和稳定缓存键；
- 为 Material、MaterialInstance、Material Graph 和 RDG 留出长期兼容边界；
- 编译失败、不支持能力、反射不一致和运行时加载失败均可诊断。

### 2.2 第一阶段非目标

- 不实现 Unity Surface Shader 的自动光照代码生成；
- 不实现 UE 的 ShaderCompileWorker 多进程协议、完整 Derived Data Cache 或宏注册体系；
- 不实现 backend-specific `SubShader`、`Fallback` 或源码中的 Vulkan/D3D 分支；
- 不实现 bindless、ray tracing、mesh shader、VRS 和 async compute；
- 不让 Shader `Pass` 表达 RDG pass、attachment、resource transition 或调度关系；
- 不在 Shipping 运行时携带 Shader 编译器；
- 不把 glslang、ShaderConductor 或 SPIRV-Cross 引入第一阶段主编译链。

## 3. 参考引擎结论

### 3.1 FlaxEngine

FlaxEngine 使用一份 HLSL 风格 `.shader` 源码声明多个 entry point 和 permutation，并为目标 `ShaderProfile` 生成平台缓存。D3D10/11 使用 FXC 路径，Vulkan 使用独立的 HLSL 到 SPIR-V 编译路径。Toy3d 借鉴其“一个 Shader 资产对应多个程序、排列和平台产物”的组织方式，但不复制其 HLSL 宏解析器和 Vulkan stage-set 布局。

### 3.2 UE4.27

UE4.27 使用 C++ `FShaderParametersMetadata` 描述参数结构，编译前生成 `/Engine/Generated/UniformBuffers/*.ush` 虚拟 include，编译后通过 `FShaderParameterMap` 保存参数名到物理位置的映射。D3D 后端读取 reflection 的 `BindPoint`，Vulkan ShaderCompiler 保存自己的 descriptor binding 映射。

UE 并未把 `View`、`Material` 等 Uniform Buffer 简单固定为全工程统一的 `b0/b1/...`。这些名称首先是逻辑资源身份，最终物理位置属于具体 Shader 和具体平台编译产物。Toy3d 采用相同的逻辑/物理解耦，但增加编译前的确定性 slot 分配和编译后的跨目标 reflection parity 检查。

## 4. 总体架构

```text
.shader / .hlsli / Material Graph
                |
                v
        ToyShader Frontend
        - ShaderLab parser
        - Properties/Resources schema
        - Pass/entry/variant/capability
                |
                v
       Shader Layout Compiler
       - ToyShaderABI packing
       - logical binding groups
       - target-independent logical layout
       - target-specific binding allocation
       - generated virtual HLSL includes
                |
                v
        Shader Compile Service
        - include/dependency tracking
        - permutation expansion/culling
        - diagnostics/debug source
          |          |          |
          v          v          v
         FXC        DXC        DXC
       DXBC SM5   DXIL SM6   SPIR-V 1.3
        D3D11      D3D12      Vulkan 1.1
          |          |          |
          +----------+----------+
                     |
                     v
          Logical + Native Reflection
          - parity validation
          - stable hashes/layout hashes
                     |
          +----------+----------+
          |                     |
          v                     v
 Verified ShaderMapEntry   ShaderCodeLibrary Records
          |                     |
          |                     v
          |              Cook ShaderCodeLibrary
          |                     |
          +----------+----------+
                     |
                     v
            Material / Renderscene
                     |
                     v
        RHIShader + RHIBindingLayout
                     |
                     v
        Vulkan / D3D11 / D3D12 backend
```

职责边界：

- Shader Frontend、layout、codegen 和目标编译器只属于 `engine/tools/shader_compiler/`；ShaderPackage loader、ShaderCodeLibrary 和参数运行时属于 RenderCore，二者都不属于 RHI；
- RHI 不读取 `.shader`、不处理 include、不展开 permutation、不理解 Material；
- RHI 只接收目标字节码、entry point、精简 reflection、稳定 content hash 和 Binding Layout；
- Material 系统操作参数 ID、逻辑分组和 Shader permutation，不操作物理 slot；
- renderscene/RDG 选择 Shader `Pass`，并补充实际 attachment format、sample count 和 render-pass scope；
- backend 只负责把公共 RHI Shader、Binding 和 PSO 翻译为原生对象。

### 4.1 工程目录与模块边界

Shader 源资产、离线工具链和 Shipping runtime 必须分开，推荐目录结构如下：

```text
engine/
├── shader/                              # 第一方 Shader 源资产与 Cook 入口
│   ├── CMakeLists.txt
│   ├── format/                          # Compiler/Runtime 共享的持久化 contract
│   │   ├── shader_format_types.h
│   │   ├── shader_map_entry.h/.cpp
│   │   └── sha256.h/.cpp
│   ├── builtin/                         # 引擎内置 .shader
│   │   ├── surface/
│   │   │   └── unlit.shader
│   │   └── post_process/
│   └── include/                         # 引擎共享 HLSL include
│       ├── ToyCommon.hlsli
│       ├── ToyView.hlsli
│       └── ToySurface.hlsli
├── tools/
│   ├── CMakeLists.txt
│   └── shader_compiler/                 # 只在开发机/Cook 构建的工具
│       ├── CMakeLists.txt
│       ├── frontend/                    # lexer、parser、AST、diagnostics
│       ├── layout/                      # ToyShaderABI、Binding allocator
│       ├── codegen/                     # generated HLSL 与 target decoration
│       ├── compiler/                    # DXC、FXC adapter 与 compile service
│       ├── reflection/                  # SPIR-V、DXBC、DXIL reflection
│       ├── package/                     # ShaderPackage writer
│       ├── cache/                       # include graph 与 derived-data cache
│       ├── tests/
│       │   └── data/                    # parser/compiler 正反例 .shader
│       └── main.cpp                     # Toy3dShaderCompiler CLI
├── runtime/
│   └── rendercore/
│       └── shader/                      # Shipping 可用、无 compiler 依赖
│           ├── shader_map.h/.cpp
│           ├── shader_map_loader.h/.cpp
│           ├── shader_map_program.h
│           ├── shader_parameters.h/.cpp # metadata、canonical encoder 与 typed binding
│           └── loaders/
│               ├── shader_map_entry_loader.h/.cpp
│               └── shader_code_library_loader.h/.cpp
└── thirdparty/                          # 锁定版本的 DXC 等上游依赖
```

目录职责固定如下：

| 内容 | 位置 | 是否进入 Shipping runtime |
|---|---|---|
| 引擎内置 `.shader` | `engine/shader/builtin/` | 源码默认不进入；只进入 Cook package |
| 公共 `.hlsli` | `engine/shader/include/` | 同上；Full debug 模式可作为 debug artifact 携带 |
| ShaderMapEntry format/types/hash/reader | `engine/shader/format/` | contract 可进入；Entry Loader 按构建选项裁剪 |
| parser、AST、layout、codegen | `engine/tools/shader_compiler/` | 否 |
| generated C++ parameters headers | `<build>/generated/shader_parameters/` | 编译进直接消费者；文件本身不提交 |
| DXC/FXC、reflection、package writer | `engine/tools/shader_compiler/` | 否 |
| ShaderMap、Loader、ShaderCodeLibrary | `engine/runtime/rendercore/shader/` | 是；Entry Loader 可裁剪 |
| RHI Shader/Binding 公共接口 | `engine/runtime/drivers/rhi/` | 是 |
| Vulkan/D3D 原生 Shader 对象 | 对应 `engine/runtime/drivers/<backend>/` | 是，仅加入所选 backend |
| 编译器测试 Shader | `engine/tools/shader_compiler/tests/data/` | 否 |

`engine/runtime/rendercore/shader/` 不包含 `dxcapi.h`、`d3dcompiler.h`、SPIRV-Reflect 或任何后端原生类型。`shader_package_format.h` 只定义版本化的定宽磁盘格式和共享枚举；writer 位于工具侧，runtime 只有 reader，避免把编译器依赖链接进 `Toy3dRuntime`。

原 Vulkan 闭环使用的 `engine/shader/test_pass.vert`、`test_pass.frag` 与 `glslangValidator` 自定义命令已经删除。当前 test pass 的唯一源码为 `engine/shader/builtin/test/test_pass.shader`；`Toy3dShaders` 使用锁定的 `Toy3dShaderCompiler` bundle 生成已验证 loose `ShaderMapEntry`。禁止继续在 `engine/shader/` 根目录新增业务 Shader。

### 4.2 `.shader` 资产位置与虚拟路径

第一方引擎 Shader 源文件统一提交到：

```text
engine/shader/builtin/<domain>/<name>.shader
```

共享 include 统一提交到：

```text
engine/shader/include/<name>.hlsli
```

工具链将它们映射为稳定虚拟路径：

```text
/Engine/Shaders/<domain>/<name>.shader
/Engine/ShaderIncludes/<name>.hlsli
```

源码中的 include 必须使用虚拟路径，例如 `#include "/Engine/ShaderIncludes/ToySurface.hlsli"`，不得写本机绝对路径或相对构建目录。这样 cache key、diagnostics、RenderDoc 源码映射和不同平台 Cook 才保持一致。

未来引入独立游戏工程后，项目 Shader 放在 `<project>/asset/shaders/`，映射为 `/Project/Shaders/`；项目目录不能反向写入 `engine/shader/`。在项目资产系统落地前，Toy3d 仓库内只启用 `/Engine/...` 根。

以下位置禁止保存手写 Shader 源码：

- `engine/runtime/generated/`：仅兼容现有生成头文件，Shader 生成物不得写入源码树；
- `engine/asset/`：保留图标、字体等普通运行资源，不与 Shader 源码混放；
- `bin/` 和 `build/`：只保存可再生成的 package、cache、debug artifact 或中间文件。

### 4.3 构建目录、Cook 产物与运行时部署

所有生成物写入构建目录或 `saved`，不写回 `engine/shader/`：

```text
<build>/generated/shader/<compile-key>/
├── ToyBindings.generated.hlsli          # target-specific 虚拟 include 的落盘副本
├── preprocessed.hlsl                    # 仅 Symbols/Full 策略保留
├── shader.spv|shader.dxbc|shader.dxil   # 中间 target binary
└── reflection.json                      # 可选诊断副本，不是运行时权威格式

<build>/generated/shader/packages/<platform>/
└── builtin.toyshaderpackage             # Cook 输出

<build>/generated/shader_parameters/
├── builtin_shader_parameters.generated.h
└── <shader-key>.generated.h              # 每个生成单元一个自包含 C++17 header

bin/shader/<platform>/
└── builtin.toyshaderpackage             # POST_BUILD 部署，runtime 只读

bin/saved/shader_cache/                   # Editor/Development 编译缓存
bin/saved/shader_debug/<compile-key>/     # 可选源码、PDB/debug artifact
```

`ShaderPackage` 是运行时唯一权威输入。Loose `.shader`、generated HLSL、reflection JSON 和单独的 `.spv/.dxbc/.dxil` 都不能成为 Shipping 加载路径。

### 4.4 CMake target 与依赖方向

使用以下 target 边界：

```text
Toy3dShaderCompilerCore   STATIC   parser/layout/codegen/compiler/reflection/writer
Toy3dShaderFormat         STATIC   ShaderMapEntry format/types/hash/strict reader
Toy3dShaderCompiler       EXECUTABLE，链接 Toy3dShaderCompilerCore
Toy3dShaders              CUSTOM，调用 Toy3dShaderCompiler Cook 内置资产
Toy3dRuntime              仅包含 rendercore/shader reader/library，并依赖 Toy3dShaders
Toy3dEditor               链接 Toy3dRuntime；需要源码编译时启动 compiler 进程
```

依赖方向为：

```text
Toy3dShaderCompilerCore -> Toy3dShaderFormat <- Toy3dRuntime
Toy3dShaderCompiler -> ShaderPackage format contract <- Toy3dRuntime
Toy3dShaders -> Toy3dShaderCompiler
Toy3dRuntime -> Toy3dShaders
Toy3dEditor -> Toy3dRuntime
```

Editor 第一阶段通过子进程调用 `Toy3dShaderCompiler`，不直接链接 `Toy3dShaderCompilerCore`。这样 Shipping runtime 不携带 compiler DLL，Editor 崩溃边界更清晰，后续也能自然演进为类似 UE ShaderCompileWorker 的独立进程池。

`Toy3dShaderFormat` 只依赖共享 Core contract；`Toy3dShaderCompilerCore`/`Toy3dShaderCompiler` 可以依赖 `Toy3dShaderFormat`，但不得依赖 `Toy3dRuntime`、RenderCore、RenderScene、公共 RHI 或任一 backend。Runtime/RenderCore 可以单向依赖 `Toy3dShaderFormat`。C++ parameters codegen 只输出文本；generated header 通过显式 custom command/output/dependency、`GENERATED` 属性、实际消费者的 `target_sources()` 和 `Generated Files\\Shader Parameters` source group 接入，不能形成 Compiler→Runtime 链接环。

`engine/CMakeLists.txt` 的长期子目录顺序应为 `thirdparty`、`tools`、`shader`、`runtime`、`editor`。`engine/shader/CMakeLists.txt` 只登记 Shader 源依赖和 Cook custom command；不得包含 parser/compiler C++ 实现。所有 custom command 使用 `$<TARGET_FILE:Toy3dShaderCompiler>`、`${CMAKE_COMMAND} -E` 和构建目录输出，并把 `.shader`、递归 include、compiler target 及版本清单列入依赖。

## 5. Shader 资产格式

### 5.1 文件类型

- `.shader`：Shader 资产容器，包含 Properties、Resources、Variants、Pass 和 HLSL block；
- `.hlsli`：共享 HLSL include；
- `.hlsl`：可选的外部纯 HLSL 实现文件；
- `.toyshaderpackage`：Cook 后运行时产物；v1 使用该扩展名，binary header 另有独立的 format version。

`.shader` 使用专用 tokenizer/parser。`HLSLPROGRAM`、`HLSLINCLUDE` 到对应结束标记之间的内容作为不透明文本处理，Frontend 不解析完整 HLSL 语法，只提取 Toy3d 自己定义的 `#pragma`。

### 5.2 最小完整示例

```hlsl
Shader "Toy3d/Surface/Unlit"
{
    Version 1

    Properties
    {
        base_color
        (
            "Base Color",
            Color
        ) = (1.0, 1.0, 1.0, 1.0)

        base_color_texture
        (
            "Base Color Texture",
            Texture2D
        ) = "white"

        material_sampler
        (
            "Material Sampler",
            Sampler
        ) = LinearWrap

        roughness
        (
            "Roughness",
            Range(0.0, 1.0)
        ) = 0.5
    }

    Variants
    {
        USE_VERTEX_COLOR : bool = false
    }

    Pass "Forward"
    {
        Requires GraphicsBaseline

        Cull Back
        DepthTest GreaterEqual
        DepthWrite On
        Blend Off

        HLSLPROGRAM

        #pragma vertex vs_main
        #pragma pixel ps_main

        #include "/Engine/ShaderIncludes/ToySurface.hlsli"

        struct vertex_input
        {
            float3 position : POSITION0;
            float3 normal : NORMAL0;
            float2 texcoord : TEXCOORD0;

        #if USE_VERTEX_COLOR
            float4 color : COLOR0;
        #endif
        };

        struct vertex_output
        {
            float4 position : SV_Position;
            float3 world_position : TEXCOORD0;
            float3 world_normal : TEXCOORD1;
            float2 texcoord : TEXCOORD2;

        #if USE_VERTEX_COLOR
            float4 color : COLOR0;
        #endif
        };

        vertex_output vs_main(vertex_input input)
        {
            vertex_output output;
            const float4 world_position =
                mul(toy_object_to_world, float4(input.position, 1.0));

            output.position = toy_to_clip_space(
                mul(toy_view_projection, world_position));
            output.world_position = world_position.xyz;
            output.world_normal = normalize(
                mul((float3x3)toy_object_to_world, input.normal));
            output.texcoord = input.texcoord;

        #if USE_VERTEX_COLOR
            output.color = input.color;
        #endif

            return output;
        }

        float4 ps_main(vertex_output input) : SV_Target0
        {
            const float4 texture_color = base_color_texture.Sample(
                material_sampler,
                input.texcoord);

            half3 final_color =
                (half3)(texture_color.rgb * base_color.rgb);

        #if USE_VERTEX_COLOR
            final_color *= (half3)input.color.rgb;
        #endif

            return float4(
                (float3)final_color,
                texture_color.a * base_color.a);
        }

        ENDHLSL
    }
}
```

### 5.3 `Properties`

`Properties` 同时描述：

- Material Editor/Inspector 中的显示名称、类型、范围和默认值；
- MaterialInstance 的稳定参数身份；
- Material constant buffer 的值成员；
- Material texture、buffer 和 sampler 资源；
- ShaderCompiler 生成 HLSL 声明所需的逻辑 schema。

第一阶段支持：

```text
Float
Float2
Float3
Float4
Color
Matrix4x4
Range(min, max)
Texture2D
TextureCube
Sampler
ComparisonSampler
```

`Float/Vector/Color/Matrix/Range` 进入 Material constant buffer。Texture 和 sampler 进入 `Material` Binding Group，但不占用 constant buffer 字节。`.shader Properties` v1 不开放 buffer、texture array 或 storage resource；这些能力通过受控 `Resources` 类型声明，写资源还必须 capability-gated。

Sampler preset 固定为 PointClamp、PointWrap、LinearClamp、LinearWrap、TrilinearClamp、TrilinearWrap、ShadowCompareClamp。Sampler 是动态参数，可被 MaterialInstance 覆盖并由 device cache 按 `RHISamplerDesc` 去重；Texture 不隐式生成 Sampler。

属性使用稳定 `ShaderParameterId`，算法见 1.1。重命名视为删除旧参数并新增参数，旧 override 作为 orphan 暂存；后续只增加显式 alias 迁移机制。

### 5.4 `Parameters`

Renderer-owned、非Material的逐Pass数值常量通过独立section声明：

```hlsl
Parameters
{
    Pass
    {
        exposure_ev : Float = 0.0
        projection : Float4x4
    }
}
```

首阶段只允许`Pass` Binding Group，以及`Float`、`Float2`、`Float3`、`Float4`和`Float4x4`。默认值可省略，省略时发布全零字节；显式默认值必须具有与类型完全一致的有限数值分量。成员按源码顺序打包，同一Shader的Pass参数自动聚合为一个ToyShaderABI constant buffer，并继续遵守每group 16 KiB上限。

`Parameters`不提供Inspector元数据或MaterialInstance override，因此不能替代`Properties`；它也不声明texture、sampler或buffer，因此不能替代`Resources`。`Global`、`View`和`Object`的常量仍来自引擎固定schema。首阶段不开放自定义Material参数、数组、struct、多个自定义constant buffer、native `cbuffer`、`register`或descriptor set语法。

### 5.5 `Resources`

非材质资源通过逻辑分组声明：

```hlsl
Resources
{
    Pass
    {
        scene_color : Texture2D<Float4>
        source_sampler : Sampler = LinearClamp
    }

    Object
    {
        skinning_matrices : StructuredBuffer<Float4x4>
    }
}
```

`Resources` 类型是封闭、可扩展的 Toy type schema，完整 v1 清单以 EBNF 为准，不接受任意 HLSL type text或 user-defined struct。`Global`、`View`、`Object` 的公共常量数据由引擎 `.hlsli` 和对应 schema 提供，避免每个 Shader 重复声明。高级 Shader 可以声明额外受控资源，但所有资源仍必须进入确定性布局编译和 reflection 验证。

### 5.6 `Variants`

第一阶段提供有类型的 permutation domain：

```hlsl
Variants
{
    USE_NORMAL_MAP : bool = false

    LIGHTING_MODEL : enum
    {
        Unlit,
        DefaultLit,
        ClearCoat
    } = DefaultLit
}
```

只有影响生成代码、entry 可用性或资源布局的条件进入 permutation。材质数值、纹理对象和可以低成本由 uniform 控制的普通分支不得制造 permutation。

后续可以增加类似 Unity 的两种策略：

- `ShaderFeature`：只 Cook 实际被 Material 使用的组合；
- `MultiCompile`：Cook 引擎要求的全部组合。

第一阶段统一按普通 `Variants` 处理，但序列化格式和 cache key 需要允许后续加入策略字段。Variant 与 enum value 使用稳定 ID，permutation key 按 VariantId 确定性序列化；源码显示顺序不改变身份。默认 soft limit 256/Pass、hard limit 4096/Pass，先做 capability culling 再计数。

Variant ABI v1 固定如下：

- `ShaderVariantId` 与 `ShaderEnumValueId` 均为 64-bit FNV-1a，0 保留为 invalid。输入按 little-endian `uint32` 长度前缀编码：`ShaderVariantId` 哈希 domain tag `Toy3dShaderVariant` 与 variant name；`ShaderEnumValueId` 哈希 domain tag `Toy3dShaderVariantValue`、所属 `ShaderVariantId` 和 option name。名称大小写敏感；声明重排不改变 ID，重命名视为删除旧身份并新增身份，类型变化保留 VariantId 但会改变 permutation record kind。
- Variant ID algorithm version 与 permutation ABI version 独立为 v1。compiler 先按 schema 补齐未显式选择项的 default，拒绝未知 variant、重复选择、非法 bool/enum value 和任何 ID collision，再按 `ShaderVariantId` 升序序列化。key 输入为 little-endian `variant_id_version + permutation_version + record_count`，每条记录写入 `VariantId + value kind`，bool 写 `uint32 0/1`，enum 写 `ShaderEnumValueId`；最终 `ShaderPermutationKey` 为该字节串的 SHA-256。空 domain 也产生带版本的非零 key。
- compiler-owned `/Generated/ToyShaderPrelude.hlsli` 只由 typed selection 生成。bool 输出 `TOY3D_VARIANT_<VariantName>` 为 0/1；enum 输出同名 selected-value macro，并为每个 option 输出 `TOY3D_VARIANT_<VariantName>_<OptionName>`。enum option 按 `ShaderEnumValueId` 排序后分配从 0 开始的 dense integer，因此源码重排不改变 generated HLSL。所有生成 macro 必须检查完整名称 collision；用户源码不得定义或声明保留的 `TOY3D_` 前缀。
- `ShaderProgramCompileInput` 和 CLI 只接受 `name=value` typed selection；缺省使用 schema default，不接受调用方注入自由形式 permutation prelude。`ShaderMapEntry` 必须分别保存 Variant ID algorithm version、permutation ABI version 与 key，ShaderMap key、package record 和 runtime 查询都使用该身份。

### 5.7 `Pass`

Shader `Pass` 是一个 Shader Program 与跨 API PSO 模板，不是 RDG pass。一个 `Pass` 至少描述：

- 名称；
- vertex/pixel 或 compute entry；
- capability requirement；
- permutation domain；
- topology、rasterization、depth/stencil、blend 和 color write mask 等可移植状态。

允许进入 Shader `Pass` 的状态包括：

```text
PrimitiveTopology
Cull
FrontFace
Fill
DepthTest
DepthWrite
Stencil
Blend
ColorWrite
```

不允许进入 Shader `Pass`：

```text
attachment resource
attachment format
sample count
load/store
clear value
resolve target
resource transition
RDG dependency
queue selection
执行顺序
```

最终 `RHIGraphicsPipelineDesc` 由 Shader Pass template 与 renderscene/RDG 提供的 attachment compatibility 合并而成。

Pass state 省略时规范化为 TriangleList、Cull Back、FrontFace CounterClockwise、Fill Solid、DepthTest GreaterEqual、DepthWrite On、Stencil Off、Blend Off、ColorWrite RGBA。省略与显式默认产生相同 template hash，重复 state 是错误。v1 的 Blend/ColorWrite 统一作用于所有 active color attachments，不支持 indexed/independent blend；Stencil reference 和 blend constants 保持 dynamic state。

### 5.8 HLSL block 与 pragma

第一阶段支持：

```hlsl
#pragma vertex entry_name
#pragma pixel entry_name
#pragma compute entry_name
```

graphics pass 必须且只能声明一个 vertex，pixel 可选，不得声明 compute；compute pass 必须且只能声明一个 compute，不得声明 vertex/pixel，也不得声明 graphics state。重复 stage、缺失 entry、未知 pragma 或 entry reflection 与声明 stage 不一致均为编译错误。

`HLSLINCLUDE` 定义 Shader 内多个 Pass 共享的 HLSL，`HLSLPROGRAM` 定义当前 Pass 的 HLSL。Frontend 合并公共 block、当前 Pass block、generated include 和外部 include，并使用 `#line` 保持诊断映射。

## 6. Properties、Parameters 与 Resources 自动生成

### 6.1 生成流程

以一个使用 `View + Material + Object` 的 graphics pass 为例：

1. Frontend 解析 `Properties`、`Parameters`、`Resources`、Pass 和 entry；
2. 参数布局器按 ToyShaderABI 分组打包 Material 数值属性、Pass数值参数与引擎固定schema；
3. Binding Layout Compiler 收集当前 Program 需要的所有逻辑 Binding；
4. 根据 target 分别生成 D3D stage/register-class mapping 与 Vulkan four-set mapping；
5. 为每个 target 生成带显式 `register()`、必要 `packoffset()` 和目标 Binding decoration 的虚拟 `.hlsli`；
6. 将虚拟 include 注入最终 HLSL；
7. 对每个 target/permutation 编译；
8. 从最终字节码提取 reflection；
9. 验证 reflection 的逻辑身份、类型、constant layout、stage visibility 和当前 target mapping；
10. Editor 写入 `ShaderMapEntry`，Cook 阶段再汇集为 ShaderPackage/ShaderCodeLibrary。

### 6.2 示例布局

输入属性：

```text
base_color : Color
roughness  : Float
base_color_texture : Texture2D
material_sampler   : Sampler
```

逻辑 Material constant buffer：

```text
base_color offset 0, size 16
roughness  offset 16, size 4
total size 32
```

如果当前 Program 只使用 `View + Material + Object`，可以生成：

```text
b0 = View
b1 = Material
b2 = Object
t0 = base_color_texture
s0 = material_sampler
```

另一个只使用 Material 的 Program 可以生成：

```text
b0 = Material
t0 = scene_color
s0 = source_sampler
```

因此逻辑 group 稳定，`b/t/s/u` slot 只需在当前 Program 内确定，不需要跨 Program 固定。

### 6.3 生成的虚拟 HLSL

生成文件写入构建或缓存目录，不写入源码目录：

```text
build/generated/shader/<shader-hash>/ToyBindings.generated.hlsli
```

示例内容：

```hlsl
#ifndef TOY_BINDINGS_GENERATED
#define TOY_BINDINGS_GENERATED

cbuffer toy_view_data : register(b0)
{
    column_major float4x4 toy_view_projection;
    float3 toy_camera_position;
    float toy_view_padding;
};

cbuffer toy_material_data : register(b1)
{
    float4 base_color : packoffset(c0);
    float roughness : packoffset(c1.x);
};

cbuffer toy_object_data : register(b2)
{
    column_major float4x4 toy_object_to_world;
};

Texture2D<float4> base_color_texture : register(t0);
SamplerState material_sampler : register(s0);

#endif
```

实际生成器必须依据目标编译器语义生成合法 HLSL。D3D11/D3D12 版本使用各自 target mapping 生成 `b/t/s/u` register；Vulkan 版本显式生成与 Vulkan backend ABI 一致的 descriptor set/binding decoration。不同 target 的物理数字无需相同。Debug 输出保留各 target 的最终生成文件，便于 RenderDoc 和编译诊断展示。

例如 `base_color_texture` 的公共身份由 `ShaderParameterId + Material + Texture + visibility` 表示。若 D3D target allocator 为当前 stage 分配到 `t0`，则生成：

```hlsl
Texture2D<float4> base_color_texture : register(t0);
```

若 `base_color_texture` 在 Vulkan Material physical set 中确定性排序后位于 binding 2，Vulkan target 则生成：

```hlsl
[[vk::binding(2, 2)]]
Texture2D<float4> base_color_texture : register(t0);
```

这里的 `register(t0)` 只是 Vulkan DXC 编译单元所需的辅助 annotation，`vk::binding` 控制 SPIR-V 原生 decoration；两者都不是跨 target 公共身份，也不能互相反推。

## 7. Binding 模型与 slot 分配

### 7.1 逻辑 Binding Group

```text
Global   跨 view、pass、material 和 object 的低频全局数据
View     camera/view 数据
Pass     当前渲染 pass 数据与输入资源
Material Material/MaterialInstance 参数与资源
Object   当前 draw/object 数据
```

Group 只表达更新频率、所有权和运行时数据来源，不创建独立寄存器命名空间，也不等于 descriptor set。公共 RHI 不暴露 Vulkan descriptor set、D3D12 register space 或 root parameter。

### 7.2 三层布局与物理 slot 规则

- logical schema 保存参数身份、group、类型、数组、constant offset 与默认值；不包含 native slot；
- active Program layout 剔除未使用的独立资源；一个 group 任一 cbuffer member 被使用时保留完整 cbuffer，整个 group 未使用才剔除；
- target-specific native mapping 为每个 Program、permutation 与 target 单独生成并保存在 package；
- D3D11/D3D12 对每个 stage、每个 `b/t/s/u` register class，按 group 固定顺序与稳定参数 ID 从 0 连续分配；stage 可以复用相同数字 slot；
- Vulkan 在四个 physical sets 内按 logical group、descriptor type 与稳定参数 ID 从 0 连续分配；
- array 占用连续 native 范围；分配前同时检查 target profile 的 per-stage 与 pipeline limits；
- `logical_layout_hash` 不包含任何 native slot；target mapping 与 mapping version 进入 `target_binding_hash`、compile key 和 package metadata。

第一阶段不允许普通业务 HLSL 绕过 Frontend 随意声明未登记资源。高级 escape hatch 必须显式标记、提供逻辑 group，并接受同样的 reflection 与冲突验证。

### 7.3 D3D register 与 Vulkan descriptor binding 映射

D3D11/D3D12 的 `b`、`t`、`s`、`u` 是互相独立且按 stage 分离的 register class；Vulkan 的同一 descriptor set 只有一套 binding 数字。因此两者必须各自分配，不能从某个跨目标公共 slot 互相反推。

Vulkan target mapping 固定为：

| Physical set | 逻辑内容 | set 内分配 |
|---|---|---|
| 0 | Global、View | 先 logical group，再 descriptor type，再 `ShaderParameterId` |
| 1 | Pass | descriptor type、`ShaderParameterId` |
| 2 | Material | descriptor type、`ShaderParameterId` |
| 3 | Object | descriptor type、`ShaderParameterId` |

每个 set 的 binding 都从 0 连续编号，不预留 class base。ShaderCompiler 使用显式 `[[vk::binding(binding, set)]]` 或经锁定版本验证等价的 DXC 参数。mapping 规则集中定义并版本化，compiler 与 backend 不得复制两套常量。

Reflection 将 native mapping 写入 target record，并与 logical schema 核对。跨 target parity 比较参数身份、类型、array count、constant offset/stride 与 stage visibility，不比较 Vulkan `set/binding` 和 D3D `BindPoint` 数字。

### 7.4 上层绑定

RenderScene 和业务 Pass 通过 generated C++ parameters 填写强类型字段：

```cpp
TonemapPassParameters parameters;
parameters.exposure_ev = exposure_ev;
parameters.scene_color = scene_color;
parameters.scene_sampler = scene_sampler;

auto pass_binding = create_transient_shader_binding(
    device,
    context,
    parameters);
```

每个 `.shader` Pass schema 与内建 Global/View/Object schema 由同一 codegen 规则产生普通 C++17 parameters struct、不可变 `ShaderParametersMetadata` accessor 和薄 `inline encode_shader_parameters` 重载。生成文件只位于 `<build>/generated/shader_parameters/`，一个生成单元只产生一个自包含 `.generated.h`，不产生 `.cpp`。输出顺序、C++ identifier 映射与内容必须确定；非法字符、关键字和规范化碰撞直接诊断，不能追加不稳定序号。

内建 View/Object 常量的 group、名称与类型以 `engine/shader/builtin_shader_parameters.h` 中的常量表为唯一声明源。ShaderCompilerCore 直接读取该表，参数头文件生成和 Cook 编译由同一份 schema 推导；修改该头文件会重建编译器及其生成物。Global 当前没有内建常量。内建参数的 C++ 赋值应按字段名完成，不依赖声明顺序。

RenderCore 的 `ShaderParameterEncoder` 依据 metadata 逐字段写入清零的 canonical bytes，并独立收集 texture、sampler 和 buffer values；它不 raw-copy C++ object representation。所有 required resource、数组、owner/type、schema identity 和 constant ABI 的确定性检查必须发生在 upload 前。Pass-local 值使用 `create_transient_shader_binding()`；persistent owner 使用相同编码 contract 的 persistent 创建入口。RHI失败保留原始错误码。

Global、View、Pass、Material、Object 只共享 schema/编码机制，不共享 owner 协议：View 每 `ViewInfo` 每帧创建一次，Object 在 frame-local draw data 中跨 BasePass/ShadowPass 复用，Material 从完整 schema 构建 persistent Program-independent logical superset，Pass 才创建 transient binding。正式 RenderScene 代码禁止直接调用 parameter ID 生成函数、填写 layout hash/Shader ABI 或构造 `RHIBindingValue`/`RHIBindingSetDesc`，也禁止使用字符串字典、无类型 variant builder、统一 `.prepare()`、`Prepared*`、binder/adapter、通用 Pass 基类或全局可变 metadata registry。

MaterialInstance 的低频 GT-facing setter 是例外的名字边界：它按 canonical name 在完整 Material schema 中解析并验证类型，只向 RT 投递已解析 ID 和 owned value；字符串不跨线程。Material 和 Shader runtime 内部仍可使用稳定 ID 做索引，但业务调用方不得提供裸 ID。

禁止上层直接使用物理 slot：

```cpp
// 禁止：上层依赖物理 slot。
binding_set->set_texture(0, base_color_texture);
```

完整 schema 将参数 ID 映射到逻辑 group、constant-buffer offset 或资源 Binding；Program active layout 只选择实际消费的子集，target mapping 才包含 native slot。RenderCore 根据 generated metadata 构造 logical binding；RHI resolver 和 backend 只消费已验证的 logical set 与 active layout。

## 8. ToyShaderABI

### 8.1 Constant buffer ABI

公共规则称为 `ToyShaderABI`，不称为 `std140` 或 `std430`。第一版以 HLSL cbuffer packing 为基线，并限制为 Vulkan、D3D11、D3D12 可稳定实现的交集。

规则：

- 基本 packing unit 为 16-byte register；
- `float` 对齐 4 bytes；
- `float2` 对齐 8 bytes；
- `float3` 占 12 bytes，但不得跨 16-byte register；
- `float4` 对齐 16 bytes；
- 数组元素 stride 向上对齐到 16 bytes；
- constant buffer 总大小对齐到 16 bytes；同一 group 的数值参数合并为一个 buffer且最大 16 KiB，禁止跨 group 合并，超限不自动分页；
- matrix 显式使用 `column_major`，禁止依赖编译器默认值；
- 生成器在需要时写出 `packoffset()`；
- 逻辑 Bool 以 UInt32 的 0/1 存储；禁止平台相关整数宽度和未定义布局的嵌套类型；
- 第一阶段 cbuffer 禁止 `half/min16float` 存储；
- CPU 侧权威格式为清零的 byte buffer + layout metadata，不依赖普通 C++ struct 的自然 padding，使用生成 schema/parameter writer 写入；
- 三目标 reflection 的 buffer size、member offset、array stride、matrix stride 必须一致。

### 8.2 Structured/Storage buffer ABI

Storage buffer 不复用 cbuffer packing。v1 使用受控 Toy StructuredBuffer ABI：

- 显式记录 `structure_stride`；
- 使用明确位宽类型；
- 禁止隐式依赖 C++ padding；
- scalar stride=4、vec2=8、vec3=16、vec4=16，matrix 每列占 16 bytes，所有 padding 清零；
- 对 DXIL/DXBC/SPIR-V reflection 的成员 offset 和 stride 做 parity 检查；
- D3D11/D3D12/Vulkan 使用相同 logical stride；实际可用数量、stage visibility 和 format support 通过 profile/capability 验证。

## 9. `float`、`half` 与公开接口精度

公开 Shader ABI 始终使用 32-bit。Material/Global/View/Pass/Object 参数、constant buffer、portable vertex input、VS→PS varying和公开 Buffer schema均使用 Float32/Int32/UInt32。

作者的局部计算可以直接使用标准 HLSL `half/halfN`。Toy3d 不提供 `toy_half` 别名，也不提供全局 half-to-float 开关；compiler、target 或 driver 可以把 `half` 提升为 FP32，缺少 native FP16 不构成 `Unsupported`。如果未来某算法严格依赖 native FP16，再引入独立 capability。

VBO storage 精度与 Shader interface 精度分离：vertex buffer 可以使用 Half、SNORM、UNORM 或 packed format，经 vertex fetch 转为 HLSL `floatN`。Reflection 必须验证 Vulkan接口没有影响该保证的 `RelaxedPrecision`，DXBC/DXIL component type 为 Float32。

## 10. 编译目标与工具链

| 目标后端 | 编译器 | Shader Model/目标 | 产物 |
|---|---|---|---|
| Vulkan | DXC | Vulkan 1.1、SPIR-V 1.3 | SPIR-V |
| D3D11 | FXC/`D3DCompile` | Feature Level 11_0、SM5 | DXBC |
| D3D12 | DXC | SM6 | DXIL |

D3D11 由 FXC/`D3DCompile` 生成 SM5 DXBC；不保留 SM4/FL10.x 路径。D3D11_SM5 支持 Compute、StructuredBuffer 与 UAV，但具体 stage visibility、slot 与 format support 仍需 profile/capability 验证，async compute 不属于基线。完整三后端 Cook 在 Windows 构建机或 CI 执行；macOS Editor 可以编译 Vulkan 目标，但不能把缺少 D3D 工具伪装成完整平台 Cook 成功。

编译器接口建议：

```cpp
enum class ShaderTarget
{
    VulkanSpirV,
    D3D11Dxbc,
    D3D12Dxil
};

struct ShaderCompileRequest
{
    std::string virtual_source_path;
    std::string entry_point;
    RHIShaderStage stage;
    ShaderTarget target;
    std::vector<ShaderDefine> defines;
    ShaderCompileOptions options;
};

struct ShaderCompileOutput
{
    std::vector<std::uint8_t> bytecode;
    ShaderReflection reflection;
    ShaderDependencyList dependencies;
    ShaderDiagnosticList diagnostics;
    ShaderContentHash content_hash;
};
```

具体 compiler release、flags 和目标版本必须进入 cache key。升级编译器时不得复用旧缓存。

## 11. Reflection 与一致性验证

Reflection 必须来自最终目标字节码，而不是只解析 HLSL 源码。

统一 reflection 至少包含：

```cpp
struct ShaderResourceBinding
{
    ShaderParameterId parameter_id;
    std::string name;
    RHIBindingGroup group;
    RHIResourceBindingType type;
    std::uint32_t target_binding;
    std::uint32_t array_count;
    RHIShaderStageFlags stages;
    std::uint32_t constant_buffer_size;
    ShaderDataLayoutHash data_layout_hash;
    std::uint32_t shader_abi_version;
};

struct ShaderConstantMember
{
    ShaderParameterId parameter_id;
    std::string name;
    ShaderValueType type;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t array_stride;
    std::uint32_t matrix_stride;
};
```

constant buffer的data layout hash覆盖ABI version、group、buffer ID、总size和每个member的
ID/type/offset/size/array stride/matrix stride。还需要记录vertex input semantic、compute
thread-group size、stage input/output和capability requirement。

Cook 必须拒绝：

- 预期 Binding 在最终字节码中逻辑身份、类型、数组长度或 target native mapping 不符；
- DXBC、DXIL、SPIR-V 的 logical constant layout 不一致；
- vertex output 与 pixel input 不兼容；
- permutation 在某个必需 target 编译失败；
- 目标使用未声明或不支持的 capability；
- 同一 stage 的 Binding 范围冲突；
- Vulkan 原生 `set/binding` 与 package 的 target mapping version 或预期 mapping 不一致；
- Material schema 与生成 HLSL reflection 不一致。

编译器可能消除未使用资源。布局验证器应区分“schema 中声明但该 stage 未使用”与“使用了但 reflection 缺失”；最终 stage visibility 以 reflection 为准，Program layout 以各 stage 的使用并集为准。

### 11.1 Editor `ShaderMapEntry` loose format 与 cache hit

Editor loose `ShaderMapEntry` v2 是原子发布目录，不是 Shipping package。目录名为调用方期望的 `shader_map_key`；reader 必须从该 key 定位目录并重新计算身份，禁止信任目录内自报 key 或扫描任意目录猜测 Entry。

v2 reader contract 固定如下：

- required manifest 使用 UTF-8、LF 和唯一 `key=value` 字段；mapping、reflection 与 dependencies 使用版本化、定界字段。v2 未知字段、重复字段、缺字段、非 canonical 十进制/小写 SHA-256、非法 enum/flags、重复 stage、重复 Vulkan set/binding、非规范虚拟路径和未排序 dependency 均拒绝。目录可以携带未来约定的 debug 辅助文件，但不能用未知文件替代 required record。
- required 文件为 `manifest.txt`、`mapping.txt`，以及每个 stage 的 `<stage>.manifest.txt`、`.spv`、`.reflection.txt`、`.dependencies.txt`。Program 只能是 vertex、vertex+pixel 或 compute；stage count、entry point、mapping stage visibility 与 reflection 必须一致。
- `shader_map_key` 重新覆盖 Shader/Pass、target/profile、mapping/layout、Pass template、Variant/permutation identity，以及按 stage 规范排序的 compile key、reflection hash 与 binary hash。`entry_content_hash` 额外覆盖带名称的 mapping 和完整 dependency records，用于区分同 key 不同内容；reflection hash 与 target binding hash 必须分别使用 compiler 的 canonical 算法重算。
- stage manifest 分别保存 binary、reflection file 和 dependencies file 的 SHA-256。reader 先限制文件大小，再读取并复核 file hash、解析 typed record、重算 semantic hash，最后重算 `shader_map_key` 与 `entry_content_hash`。v2 limits 为 manifest 64 KiB、单个 metadata 文件 4 MiB、单 stage binary 64 MiB，各类 binding/dependency/member/interface record 最多 4096。
- 首次 writer 使用 owned staging directory 和 no-replace atomic rename。目标 key 已存在或并发 rename 失败为 `AlreadyExists` 时，writer 必须调用同一 reader 完整验证已有 Entry；key 和 `entry_content_hash` 都一致才返回 `cache_hit`。已有 Entry 损坏或同 key 内容冲突必须诊断失败，不覆盖、不删除 final directory。
- reader 验证的是 artifact 完整性与指定 identity，不自行搜索源码或判断当前工程依赖是否变更。Editor compile/cache 调用方必须从当前规范化 AST、generated HLSL、toolchain identity 和 dependency content 形成期望 key；`ShaderMapEntryLoader` 只消费已由该 contract 验证的 Entry。

## 12. ShaderPackage

Cook 产物至少包含：

```text
package format version
ToyShaderABI version
target binding mapping version
shader asset identity
program/pass table
permutation table
capability requirements
portable PSO templates
parameter schemas/default values
logical binding groups
logical reflection
parameter schema、logical layout、target binding 和 bytecode content hashes
target bytecode records
source/include dependency hashes
compiler identity/version/flags
content hashes
optional debug artifacts
```

每个目标字节码记录至少包含：

```text
target binary format
stage
entry point
permutation key
bytecode
bytecode content hash
target reflection hash
```

序列化不能直接 dump 编译器或平台结构体；固定为 little-endian、定宽字段与 section table。Header/section 明确 magic、major/minor、required/optional flag、offset、size、count；reader 检查边界、重叠和整数溢出。未知 optional section 可跳过，未知 required section 或 major 不兼容必须拒绝。ABI version、ParameterId algorithm version 与 target mapping version 分开记录。v1 支持 multi-target但不压缩，Cook 可按平台/产品裁剪；runtime 依据 backend family、显式 profile和版本化 fallback chain选 record，不依赖 section 顺序。

## 13. 缓存与增量编译

Shader 编译缓存 key 至少覆盖：

- `.shader` 规范化语法树或内容；
- 当前 HLSL block；
- 所有递归 include 内容；
- generated binding include；
- target binding mapping version、logical group 与 target-specific allocation结果；
- entry point 和 stage；
- 按 VariantId 确定性序列化的 permutation；
- target、Shader Model 和 capability tier；
- compiler 名称、版本和 flags；
- Shader Frontend、layout allocator、package 和 ToyShaderABI 版本；
- debug/optimization policy。

建议分两层：

- `ShaderDerivedDataCache`：编译任务 key 到单目标输出，用于 Editor 增量编译；
- `ShaderCodeLibrary`：Cook 后面向运行时的平台字节码库，按 content hash 去重。

include dependency graph 记录虚拟路径与内容 SHA-256。文件变更只使受影响 Shader/permutation 失效。并发 cache miss 使用 single-flight，避免同一任务重复编译。diagnostic 数量上限不进入合法输出 hash。

## 14. 调试、RenderDoc 与热重载

### 14.1 加载和调试模式

```cpp
enum class ShaderLoadMode
{
    Precompiled,
    RuntimeCompile,
    Auto
};

enum class ShaderDebugMode
{
    None,
    Symbols,
    Full
};
```

建议：

| 构建模式 | LoadMode | DebugMode |
|---|---|---|
| Debug Editor | `Auto` | `Full` |
| Development | `Precompiled` 或 `Auto` | `Symbols` |
| Shipping | `Precompiled` | `None` |

GPU 始终执行 binary。“源码模式”表示 Editor/Development 可以从 HLSL 重新生成 binary；“源码调试”表示 binary 或外部 symbol 中保留源码、行号和调试映射。

Debug artifact 保存：

- 原始 `.shader`；
- 提取后的 HLSL；
- generated `.hlsli`；
- 预处理后的最终 HLSL；
- `#line` 映射；
- DXBC/DXIL/SPIR-V debug symbols；
- binary content hash。

Debug 使用低优化、embedded debug 和完整源码；Development 使用正常优化与外置 `.toyshadersymbols`，按 content hash 查找；Shipping strip 源码、symbols 和本机绝对路径。strip 后计算最终 bytecode hash。

### 14.2 热重载

- 编译在 Shader compile worker thread/pool 执行，不阻塞 Render Thread；
- 有 last-known-good 时编译失败继续使用旧 Shader/PSO；首次失败使用 pass-family compatible Error Shader，Compute 默认跳过并诊断，核心 Global/copy/present Shader 失败不能伪装成功；
- 编译成功后在安全点原子替换 Shader Program；
- 新旧 layout hash 相同可以保留兼容参数数据；
- layout hash 改变时按 `ShaderParameterId + type` 迁移参数，类型不一致使用新默认并 warning，删除参数的 override 保留为 orphan；重建完整 Binding Layout、Binding Set 和相关 PSO；
- 新 Program/PSO 批次全部成功后才切换，任一创建失败整批回滚；
- 旧 RHI Shader、Binding 和 PSO 由 command list 强引用及 queue completion value 延迟释放；
- Shipping 不启用文件监听和运行时编译。

## 15. 与 Material 系统的边界

Material Graph 不生成 SPIR-V、DXBC 或 DXIL，只生成 Material HLSL 函数、Properties schema 和 static permutation。

```text
Material Graph
    |
    +-- parameter schema/default values
    +-- static switches/permutation
    +-- generated material HLSL
                    |
                    v
          Material Template .shader/.hlsli
                    |
                    v
             ShaderCompiler
```

约束：

- Material 持有 Shader 资产、permutation key 和参数默认值；
- MaterialInstance 只覆盖动态参数，不因普通数值变化重新编译；
- static switch 才产生新的 permutation；
- Material schema 保存 canonical name、稳定 ID、typed default、constant layout 与完整 schema identity；MaterialInstance setter 按 canonical name 验证后才在内部使用稳定 ID，不使用物理 slot；
- MaterialRenderProxy 从完整 schema 创建 persistent constants 与 Program-independent logical superset；Program mapping 或 active subset 变化不使该 set 失效；
- Material Shader 描述程序和逻辑资源布局；
- PSO 由 Shader Pass template 与实际 render target compatibility 共同创建；
- Material 不拥有 Vulkan descriptor set 或 D3D12 root signature；
- RDG pass 只请求某个 Shader Pass/permutation，不参与 Shader 源码编译。

## 16. 与 RHI 的接口

`RHIShaderDesc` 以 stage、bytecode、entry point、reflection 和 content hash 表达后端创建输入：

```cpp
enum class RHIShaderBinaryFormat
{
    SpirV,
    DxBc,
    DxIl
};
```

用 `RHIShaderBinaryFormat` 替代自由字符串 target。RHI reflection 只保留创建设备对象、验证 Binding Layout 和 pipeline 所需的精简信息；完整参数 member、默认值、Material schema 和 source debug metadata 留在 RenderCore/ShaderPackage。

`RHIDevice::create_shader()` 最终使用 NVI frontend，统一执行：

- binary format 与 backend/capability 匹配；
- stage、entry 和字节码合法性；
- content hash 非零且稳定；
- reflection 与 Binding Layout 一致；
- device owner identity；
- Shader object cache 去重；
- backend 创建失败返回可诊断错误。

`RHIBindingLayout` 由 Shader Program 各 stage reflection 合并生成，renderscene 不手写 layout。Shader 对象 hash、Binding Layout hash、PSO hash 和 Material 参数值 hash 必须分离。

公共 transient uniform descriptor 只含 source bytes 与诊断名，返回 slice 只含 buffer、offset 与 logical size。`ShaderParameterId`、data layout hash、Shader ABI version、generated parameters、`ShaderParametersMetadata`、`ShaderParameterEncoder`、字段地址、Pass 类型和 Material schema 不得进入公共 RHI/backend hook；RenderCore 在创建 logical uniform value 时附加兼容性 identity。Vulkan、D3D11 与 D3D12 backend 继续只接收 logical BindingSet 和当前 Pipeline active layout。

## 17. 三后端映射

| 公共语义 | Vulkan | D3D11 | D3D12 |
|---|---|---|---|
| Shader binary | SPIR-V 1.3 module | DXBC SM5 | DXIL SM6 |
| Constant buffer | uniform buffer descriptor | constant buffer stage slot | CBV/root binding |
| Texture/SRV | sampled image/buffer descriptor | SRV stage slot | SRV descriptor/root binding |
| Sampler | sampler descriptor | sampler stage slot | sampler descriptor |
| UAV/storage | capability-gated descriptor | SM5 UAV/storage slot | UAV descriptor |
| Binding Layout | backend pipeline/descriptor layout | logical stage-slot validation | root signature/descriptor layout |
| 逻辑 Binding | parameter ID、group、type、array、visibility | 同左 | 同左 |
| 原生 Binding | 四个 physical sets 内紧凑 `set/binding` | target/stage-local `b/t/s/u` slot | target/stage-local `b/t/s/u` register，root mapping 由后端生成 |
| Debug info | SPIR-V debug/source mapping | DXBC debug info | DXIL debug info/PDB or embedded |

Vulkan backend 按 `Vulkan ES3.1 profile` 将五个逻辑 group 打包到四个 physical sets，并在 set 内紧凑分配。D3D12 backend 可以生成 root signature，但 root parameter 不进入 ShaderPackage 公共 schema。D3D11 SM5 支持 compute/UAV/storage；具体 limits 与 format support 仍通过 capability/profile 验证。

## 18. 错误模型与验证

错误至少区分：

```text
ParseError
InvalidShaderAsset
InvalidPropertyLayout
BindingConflict
UnsupportedCapability
CompilerUnavailable
CompileFailed
ReflectionFailed
ReflectionMismatch
PackageVersionMismatch
PackageCorrupt
BackendRejectedBinary
HotReloadIncompatible
```

诊断包含 Shader 虚拟路径、Pass、stage、entry、permutation、target、include stack、源码行列、compiler 原始消息和 Toy3d 上下文。Release/Shipping 不能用 assert 代替可检查错误路径。

## 19. 外部依赖与下载来源

所有依赖锁定具体版本、平台包和 SHA-256；CMake 不依赖浮动 `latest`。第三方源码或 binary 放入 `engine/thirdparty/` 或由明确的依赖安装步骤提供，除依赖升级外不修改上游代码。

### 19.1 DirectX Shader Compiler

用途：D3D12 DXIL、Vulkan SPIR-V、DXIL reflection 和 debug info。

- Releases: <https://github.com/microsoft/DirectXShaderCompiler/releases>
- Command Guide: <https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/CommandGuide.md>
- SPIR-V: <https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst>
- 16-bit types: <https://github.com/microsoft/DirectXShaderCompiler/wiki/16-Bit-Scalar-Types>

Toy3d 从锁定 source commit 自行构建 `dxcompiler.dll` 及其他平台 DXC 动态库；Windows DXIL validation 使用与该构建兼容、来源锁定的 Microsoft 官方 `dxil.dll`。具体 commit、构建参数、匹配关系和文件清单记录到 bundle manifest。

### 19.2 Microsoft FXC 官方二进制

用途：D3D11 SM5 DXBC、`D3DCompile`、`D3DReflect` 和 `D3DStripShader`。

- Windows SDK: <https://learn.microsoft.com/en-us/windows/apps/windows-sdk/>
- Windows SDK downloads: <https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/>
- Windows SDK archive: <https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/>
- `D3DCompile`: <https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile>
- HLSL packing: <https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules>

正式 bundle 只接受锁定版本的 Microsoft 官方 `d3dcompiler_47.dll`，记录来源、SHA-256 与 license；普通用户不需要自行安装 Windows SDK。本地与 CI 不从系统目录或 `PATH` 偶然选择版本。

### 19.3 SPIRV-Reflect

用途：从最终 SPIR-V 提取统一 reflection。

- <https://github.com/KhronosGroup/SPIRV-Reflect>

建议静态链接到 ShaderCompiler 工具，不进入 Shipping runtime。

### 19.4 Vulkan SDK / SPIR-V Tools

用途：`spirv-val`、可选 `spirv-opt`、Vulkan headers/loader 和 validation layers。

- Vulkan SDK: <https://vulkan.lunarg.com/sdk/home>
- SPIRV-Tools: <https://github.com/KhronosGroup/SPIRV-Tools>

第一阶段 Cook 后至少运行 `spirv-val`；`spirv-opt` 可后置。

### 19.5 RenderDoc

- Releases: <https://github.com/baldurk/renderdoc/releases>
- Documentation: <https://renderdoc.org/docs/index.html>
- Shader debugging: <https://renderdoc.org/docs/how/how_debug_shader.html>

## 20. 实施时仍需记录的依赖细节

以下细节不改变本文架构，但必须在对应阶段实现前锁定并版本化：

- DXC、SPIR-V Tools、SPIRV-Reflect 的具体 source commit、构建参数与 SHA-256；
- Toy3d 自建 `dxcompiler.dll` 的 source commit/build flags，以及 Microsoft 官方 `d3dcompiler_47.dll`、`dxil.dll` 的匹配版本、下载 URL、SHA-256 与 license 清单；
- 锁定 DXC 版本下 Vulkan 1.1/SPIR-V 1.3、relaxed cbuffer layout、debug 与 optimization flags 的精确拼写；
- 实际移动设备的 limits、驱动兼容证据与 profile conformance 测试清单；
- Editor ShaderMap 和 Cook ShaderCodeLibrary 的具体磁盘分片、清理与部署策略。

这些细节若导致公共 Shader 语法、Material 参数身份、RHI Binding 语义或支持平台范围变化，必须先更新本文并重新确认；仅实现选择可以在对应阶段设计记录中确定。

## 21. 长期演进与验收

- Shader language、ToyShaderABI、parameter ID、generated format、target mapping 与 package version 均独立版本化；任何破坏旧语义的变化必须先更新本设计和对应 OpenSpec contract。
- 新能力按 Frontend/schema → codegen → target compiler/reflection → ShaderMap/package → RenderCore typed binding → RHI/backend 的单向依赖推进。Compiler/Format 不得反向依赖 Runtime，RenderScene 不得依赖 backend。
- 每个 vertical slice 必须以确定性生成、损坏输入、跨 target contract、owner/lifetime 和可诊断失败测试验收；平台运行验证与构建记录保留在任务、提交或 PR，不写入本长期设计。
- D3D11 FL11_0、D3D12、Vulkan ES3.1 profile 和移动端 limits 必须保持同一 logical schema/typed parameters contract；新增 ShadowPass 等业务 Pass 不得要求修改公共 RHI 或 Vulkan physical-set mapping。
- Cook `ShaderCodeLibrary`、完整三目标编译、热重载、RDG 与异步编译继续按各自 capability/spec 独立落地，不建立临时双轨 reader、兼容 wrapper、通用 Pass Scheduler 或全局可变 metadata registry。
