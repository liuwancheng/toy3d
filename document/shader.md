# Shader：语言、ABI、编译与运行时

构建设置使用 `engine/config/shader_build.settings`，项目可用 `project/config/shader_build.settings` 覆盖相同 target/profile/Editor policy 及相同 Shader 的额外 typed 配置。Core `ShaderBuildSettings` 复用 bounded SourceCompileRequest 编码，版本 1，最多六种 profile policy、256 个 source、总计 1 MiB；字段不接受任意 defines 或 compiler arguments。未声明的 profile 拒绝。`make_shader_source_compile_request` 合并自动收集的配置和额外配置，实际 source domain 验证/去重仍由 compiler planner 完成。

`compile-vulkan --settings <engine-file> [--project-settings <project-file>] --build-mode Editor|Player` 使用同一设置入口；不能与 `--request` 合用。CMake 内置源编译明确使用 Editor policy。Editor 在请求启动、候选加载与发布前校验 policy、额外配置和设置 revision；在途设置变化拒绝整个候选，旧集合继续使用。`cook-vulkan <engine-root> <project-root|-> <new-deployment-root> <new-work-root> Editor|Player` 收集 .asset 材质继承前缀和额外配置，发现受控 Shader 树，在创建输出之前规划全部源（单源 1024、整批 4096 Program）。输出/工作目录必须不存在且父目录已存在，两者不得与源树重叠。编译后复验资产/source/include/settings revision、entry 与完整配置覆盖，最后写 `deployment.txt`；失败目录没有部署清单，不能作为已发布结果加载。仅编译 required entries；Player 排除 HitProxy 源及所有 HitProxy roles。

部署清单 `ShaderDeployment` 为版本 1，记录同一 policy、每个 source hash、全部所需配置 key 和 required Program 数，读预算 1 MiB。`ShaderMapEntryLoader::load_deployment` 验证清单及全部 family/entry/完整 Material schema；source、policy、配置或 Program 数不一致时整批失败，输出保留原值。它是 CPU admission；RHI pipeline/资源支持仍由各实际使用者检查，不能把读取成功等同于 GPU 验证。

## 定位与当前范围

tools/shader_compiler 为 frontend/layout/codegen/CLI，rendercore/shader 为 ShaderMap/Loader/Program/typed parameters，engine/shader 与 project/shader 是源码。Toy3dShaderFormat 是独立格式库，源码位于 engine/core/shader，依赖 Core，不依赖 Runtime 或 compiler；engine/shader 只保存 shader 源码和构建规则。

Shader logical schema 是权威；reflection 验证字节码并提供 native mapping，不能反向补出未声明业务参数。RHI 不解析 .shader、C++ generated metadata 或 Material 属性。

当前生产外部编译入口 compile-vulkan；D3D11 FXC/SM5、D3D12 DXC 的工具链设计不等于已完成外部编译或后端。Cook/Player 使用 required 清单、完整集合索引和去重 stage code 的只读部署；不包含压缩 ShaderCodeLibrary。Editor 消费相同集合协议，并允许会话中验证后发布的 Saved revision。

## 语言与源码边界

语言只接受 Version 2，旧源明确失败并要求按新协议重写；packing/默认语义变更仍须提升 language/ABI major，未知关键词错误。

语法权威 [shader-language-v2.ebnf](shader-language-v2.ebnf)，可用真实示例 engine/editor/tests/fixtures/shader/painted.shader 与 engine/shader 内置文件，不写想象的 DSL。源码声明 Usage=Global/Material/MeshPass；Material 使用 Geometry Standard 或 Custom，MeshPass 使用 Custom，均显式列出 VertexFactories；Global 没有 mesh geometry。每个 Pass 的显示名与 Role 分开，独立 HLSLVS/HLSLPS/HLSLCS 块各声明一个匹配的 stage pragma，HLSLINCLUDE 不声明入口。Forward/HitProxy 必须有 VS/PS，ShadowDepth 允许只含 VS，compute 仅属于 Global。Custom 使用这些原生 stage 入口；Standard 只声明 Forward 的 HLSLPS 着色函数，编译器生成 VS/PS 包装。Features 声明接受的 engine 能力；Variants 可追加 `Stages { Pixel } Passes { Forward }`，省略时保守影响所有 stage/role。展开 include 后检测宏依赖，越界使用拒绝编译。

- Properties 是 Material 属性，保留源码顺序/typed default/UI；不是任意 HLSL struct。当前不开放属性数组，矩阵为 Matrix4x4。
- Parameters 只在 Global source 开放，首批只 Pass group 的 Float/Float2/Float3/Float4/Float4x4；省略 default 全零，显式 default 分量类型/有限性一致，不开放数组/struct。
- Resources 只在 Global source 开放；Material 使用 Properties 和 Features。Global Resources 受控 Texture2D/2DArray/3D/Cube/2DMS、Sampler/ComparisonSampler、Buffer/ByteAddressBuffer/StructuredBuffer 和 EBNF 中 RW 类型。语言识别不保证当前 backend/material 支持所有组合。
- Texture 与 Sampler 独立，不隐式生成；Texture2DMS 只 Load。Sampler preset 修改不重编，相同 descriptor device 去重；ShadowCompareClamp 使用 reversed-Z GreaterEqual。
- 不支持 descriptor arrays/bindless、任意 user struct、Append/Consume、Texture1D/CubeArray；buffer 元素数组不等于 descriptor array。
- include 只走登记源/明确虚拟根与 whitelist，规范路径、依赖摘要、深度/总量/循环均验证，不回退 cwd 或系统目录。
- 保留 TOY3D_ generated prefix，用户不得重定义；Pass/Variant/profile/capability 都进入身份或 validation，不把任意 pragma 当契约。

## 参数身份与 ToyShaderABI

ShaderParameterId 为稳定 64-bit FNV-1a，输入带长度编码的 group/category/name，ASCII 大小写敏感、0 invalid，类型不进入 ID；原始名字保留以检测 collision。同 ID 还须验证 layout hash；rename 是删除+新增，旧 Material override 成 orphan，不模糊匹配。

| 规则 | ABI |
| --- | --- |
| 标量 | Float32/Int32/UInt32，Bool 用 UInt32 0/1，little-endian |
| cbuffer | 16-byte register，成员不跨边界，float3+float 可共 register |
| 数组/矩阵 | 数组 stride ≥16；column-major 每列 16 bytes |
| group | 数值合并一 cbuffer，不跨 group；最大 16 KiB，不自动分页 |
| padding | 全零，总 buffer 16-byte 对齐 |
| StructuredBuffer | scalar=4、vec2=8、vec3=16、vec4=16，矩阵每列 16；不复用 cbuffer packing |

权威为 canonical byte buffer + metadata，禁止 memcpy 任意 C++ struct。上传 alignment 不改变成员 ABI。cbuffer 按 source/schema 顺序，重排改变 layout；独立资源按 group/class/ID 确定排序。

full schema 保留默认值；Program active layout 仅含实际使用独立资源，cbuffer 任一成员 active 即保留完整 offset/size，整个 group unused 才移除。native mapping 独立，映射见 [RHI](rhi.md)。跨 target parity 比身份/类型/count/offset/stride/stage，不比 slot。

ToyShaderABI 3 的 engine-owned Object canonical 数值包含两组 Matrix4 与 UInt32 `toy_num_bone_influences`（offset 128），总大小 144 bytes，其余为 canonical padding。阴影接收由 Primitive/View 选择 Pass 程序，不再上传重复 uniform；影响数由骨骼几何提供 4/8，仅 GPUSkin 使用，行为见 [Animation](animation.md)。4/8 不构成 shader permutation；数值成员进入 layout hash，旧产物须重新编译并与生成参数共同部署。

schema hash（含 default/UI/sampler preset）、logical layout hash、target binding hash、bytecode hash 分离；compile/package/dependency 内容寻址 SHA-256，parameter identity 保持 FNV。

## Variant/permutation

VariantId/EnumValueId 用带 little-endian uint32 长度前缀的 FNV domain；variant name 与 enum option 的稳定身份不随声明排序变更。0 invalid，rename 新身份，kind 改变进入 permutation record。

compiler 按 schema 补 defaults，拒绝未知/重复/非法选择和碰撞；按 VariantId 排序，序列化版本/count/ID/kind 与 bool uint32 0/1 或 EnumValueId，SHA-256 得 permutation key；空 domain 也有非零带版本 key。

规范化实现位于 Toy3dShaderFormat 的 `shader/shader_permutation.h`：共享 domain 与 typed selection 不依赖 AST/Runtime，Tools 的 `compiler/variant_permutation` 只转换 AST/CLI 文本并补源码位置诊断。Material/Pass scope 分离 ID 与 generated 宏；每域最多 32 维、每 enum 最多 32 值。默认值、显式 kind、孤儿/重复选择、非法声明及宏碰撞必须整体校验后才能发布结果。共享入口支持按单个 stage 投影并先验证完整配置；语言支持 Stages/Passes 声明；省略时保守影响全部。compiler 按 stage/role 投影宏及资源，但保持完整 cbuffer ABI；source job 复用相同编译请求，重做每个 Program 的反射校验。改变插值接口的选项必须影响 VS/PS，越界宏依赖及接口不匹配拒绝编译。独立 Core 验证入口为 `Toy3dShaderFormat.Permutation`，只依赖 Toy3dShaderFormat。

generated prelude 的 TOY3D_VARIANT_* 名字检查碰撞；enum options 按 ID 生成 dense integer，不依赖源码顺序。不能用任意 HLSL define 绕 typed selection/key。

mesh shader 的 engine-owned `VertexFactoryType`（None/Local/GPUSkin）是独立身份字段，不进入用户 variant 声明，也不派生或修改 Material permutation key。factory 支持由源码声明，编译请求与 Runtime 校验同一 Core contract；不按原始 include 或依赖文本猜测。GPUSkin 注入 Object 的 `toy_bone_matrices` typed resource 并生成 `GPUSkinObjectShaderParameters`，静态 schema 不要求骨骼资源。语言、集合和 Editor 允许只声明 GPUSkin；实际网格使用前检查 factory，Local 网格不能借用 GPUSkin 程序；4/8 共用 shader 见 [Animation](animation.md#公共网格边界与-permutation)。

## Typed parameters 与 Program

generated C++ 类型在 build，提供普通 metadata/encoder overload，RenderCore 用 dependent lookup 调用；无需 traits/wrapper/全局 metadata registry。Transient 参数在当前 context 分配/拷贝 uniform；persistent group 接口显式 metadata、encoder、debug_name，不虚构 .prepare() 协议。

真实 helper 实现片段，来源 engine/runtime/rendercore/shader/shader_parameters.h（parameters 类型须是生成/已定义 overload 的类型，device/context 是运行中的同 device）：

```cpp
const toy3d::ShaderParametersMetadata& metadata =
    shader_parameters_metadata(parameters);
toy3d::ShaderParameterEncoder encoder(metadata);
encode_shader_parameters(parameters, encoder);
auto binding = toy3d::create_transient_shader_binding(
    device, context, metadata, encoder);
// 调用方检查 RHIResult；失败不能继续构造 draw。
```

program-independent group snapshot 随当前 active layout 验证，不能把 Program/native slot 写进 Material 参数身份。GlobalShaderMap::load(shader_map, platform, required_types) 返回不可变 map，find(type) 查预先验证 Program；Renderer start 处理 missing/incompatible，不在 draw 注册类型或编译。

mesh role 的 Pass 参数由 compiler 统一注入：Forward 使用 Core builtin_forward_parameters/resources 的完整光照、阴影与环境 ABI；ShadowDepth 使用 shadow_world_to_clip/shadow_light_direction/shadow_bias_parameters，HitProxy 使用 hit_proxy_id_parts。Material 不允许 Parameters/Resources 或与引擎 Forward 名字冲突的 Properties，新增用户参数属于 Material。引用光照、阴影、环境字段或宏必须声明相应 Features。ForwardPassParameters 来自 builtin_shader_parameters.generated.h，不依赖 Phong/PBR 生成头；所有 Material/factories 共享完整 Pass ABI，反射决定当前 active bindings。Global 的作者参数保持既有规则；各 mesh role ABI 独立。ABI 3 要求旧程序整体重新编译，不保留旧模式 reader。

`validate_shader_parameters_metadata_against_schema` 继续验证完整 schema 身份；`validate_shader_parameters_group_against_schema` 验证版本与完整单组 ABI，供引擎 group 跨材质源复用。Editor 使用生成的完整 View/Object/role Pass metadata 检验候选，再投影 active bindings；不使用默认 Phong 的 reflection 作为其他材质的资源白名单。Custom 的 Material/其他组可以不同，不能因此跳过 Pass 字段/资源类型、数量、offset/stride/default 的校验。

## ShaderMapEntry 与缓存

目录按调用方期望 shader_map_key 定位，reader 重算身份，不相信 manifest 自报 key/扫描猜测。required 文件：manifest.txt、mapping.txt，每 stage 的 manifest/reflection/dependencies 与其引用的 stage_code binary；vertex、vertex+pixel、compute 组合需 stage/entry/mapping/reflection 一致。

entry format 10 持久化并验证 Usage、Role、Geometry、surface mode、所选 VertexFactory 及源的 factory 支持范围，这些字段均进入内容身份。Material 与 Pass 分别保存 permutation key；Program 查询和 RHI 程序缓存同时比较两者，不能把 Off 与 PCF/Sky 混用。compile-request 为版本 3；reader 只读新格式，旧 entry 重新编译。Runtime 与 Editor 必须读取集合索引，不扫描目录寻找程序；没有索引的旧产物需要重编译。

- UTF-8 LF、唯一 key=value、版本定界 record；未知/重复/缺字段、非 canonical 数字/小写摘要、非法 flags/enums/paths、重复 binding/stage、未排序 dependencies 拒绝。
- manifest ≤64 KiB，metadata 单文件 ≤4 MiB，stage binary ≤64 MiB，各类 record ≤4096。先限大小/hash，再解析/重算 semantic hash，最后重算 shader_map_key/entry_content_hash。
- shader_map_key 覆盖 Shader/Pass、target/profile、mapping/layout、template、Variant/permutation 与 stage compile/reflection/binary；entry_content_hash 另含具名 mapping/full dependency record，区分同 key 内容冲突。
- writer 用 owned staging/no-replace rename；已存在必须用同 reader 全验证且 key/content 相同才 cache_hit，损坏/冲突诊断、不覆盖/删 final。
- reader 只验 artifact，当前源码/include/toolchain identity 由 compile/cache 调用方验证；不存在“缓存文件在就可信”的捷径。

## 完整集合索引与候选

index format 5 保存 Core 的 Material permutation domain、包含默认值的 typed selections、编译 policy、Features 条件及 SupportedWhen。每个程序独立保存 Pass key 与 typed selections。读取时重新解析 Material 配置、求值条件并调用共享编译计划，检查每个 required query 恰好存在。`ShaderWorkflow::shader_map(name, selections)` 和 Game 加载按完整配置查找；不能根据同名 Shader 的目录顺序选择配置，找不到精确配置时报错。声明和选项数量、kind、名称、enum 值及 stage mask 均受共享 Core 验证。

`shader/shader_map_index.h` 是 Core 的集合协议，索引位于产物根的 `shader_maps/<configuration_key>/index.txt`。配置查询 key 覆盖逻辑 Shader 名、target/profile、已解析 Material permutation；源全文 SHA-256 独立标识 revision，不把 source revision 混入配置查询。每个编译输出根只发布该配置的一份不可变 revision；相同内容可复用，冲突保留原产物，Editor 新 revision 使用独占请求目录。

索引独立保存源码声明的 Pass 显示名/Role 列表，再记录各 Pass/VertexFactory/engine selection 对应的 entry key 与 entry content hash。覆盖范围由声明和 policy 共同决定；Global 每个具名 Pass 对应 None，mesh role 唯一，Material 必须有 Forward。缺掉 required Pass、factory 或 engine selection，重复身份、未知声明及 entry 不匹配均失败。Pass/程序各不超过 1024 项，索引不超过 4 MiB；规范排序、UTF-8 LF 与 payload 摘要同时验证，拒绝未知记录、损坏内容及链接路径。

`compile-vulkan` 使用 `plan_shader_compilation` 编译所需的 Pass、factory 和引擎选项，现有 Pass 参数用于检查请求入口是否存在。`--variant name=value` 表示单配置；`--request path` 接受 Core `ShaderSourceCompileRequest` 的版本 1 canonical 文件，包含显式 policy 和多个 typed 配置，两种入口互斥。作业先依据源码声明补默认值、去重、逐配置展开 Standard roles，再核对整源过滤前的组合预算（最多 1024 Program），完成这些检查后才调用外部编译器。该 source job request 与 entry 中的单 stage compile request 是不同职责的格式。

每个 entry 先独立完成内容寻址发布；所有配置的 entry 成功后，才逐个用 owned staging/no-replace rename 发布索引。中途失败允许留下内容寻址 entry；CLI 非零结果的作业不能成为有效发布目录。多配置原子可见性由调用方对整个不可变作业目录的发布记录保证，不能把某个索引出现当成整项作业完成。project policy 与 stage 编译复用使用上文共享入口；不另建编译器或配置身份。

Core `read_shader_map_indices` 在加载边界有界枚举并验证整个源码配置集合：目录身份必须与 typed key 一致，索引须为普通文件，合计最多 16 MiB、1024 Program，配置不能重复，source revision、profile、domain、features 和 policy 必须一致；错误不发布部分读取结果。Runtime 的完整 Material schema 与 GPU pipeline 验证仍由其 candidate admission 承担。

Runtime 的 `ShaderMapEntryLoader::load_collection` 验证索引及全部 entry，失败不返回部分程序。`ShaderMapCollection::create_candidate` 再检查程序契约、完整覆盖与 schema，产生共享不可变集合；同一 Material 的全部 roles/factories 保持完整 Material schema，同 Pass 的 factories 保持 Pass schema/graphics state。GPUSkin peer 必须具有 Object 的骨骼 typed buffer 与两组 influence 输入。`find(role, factory, global_pass_name, pass_selections)` 重新规范化 Pass 选择并精确查找，默认值从对应 domain 解析；Global 还须显式提供 Pass 显示名，缺项不回退。`MeshBatch::material_program` 根据已解析 features、Primitive 接收开关、View 阴影有效性和 policy 选择 PCF/Off；Environment 根据 feature、policy 和当前场景有效环境选择 Sky/Off；配置资源加载或上传失败明确诊断，不当成未配置。

集合内 Local/GPUSkin 是独立 Program，没有互相挂接。`ShaderMap::find_or_load_collection` 按逻辑名/profile/Material permutation 缓存不可变集合，MaterialDesc/Proxy/Library/Editor 接管整个集合。Base/Shadow/HitProxy 从实际 owner 精确查询角色与 factory；旧集合/Program refs 维持既有 GPU 生命周期。GlobalShaderMap 只接纳 Usage Global、Role Global、factory None，Tonemap/ImGui 继续用显式 GlobalShaderType 查询；HitProxy 与 ShadowDepth 由内置 MeshPass 集合持有。单 Program 的 load_program 仍供 Global/cache 等明确 entry 查询使用，先验证完整索引，但不建立其他 Program 附属关系。

按所选程序创建 typed binding 时，`ShaderParameterEncoder(metadata, program_data)` 先核对完整 group schema，再投影 active constant buffer/resources。生成代码仍读取完整字段，未使用的资源可为空；active 必需资源错误在上传前失败。`binding_metadata()` 提供不可变的 active 声明及独立 group identity，资源与常量偏移不重新打包。Forward lighting 在每个 View 内按 active group identity 缓存，BasePass 消费对应程序的绑定，Off 不需要 dummy shadow atlas。Material Proxy 同样按 active Material group identity 与实际参数/view 状态缓存各程序绑定；细节见 [Material](material.md#绘制查询与参数绑定)。

## Editor 全量重编译

内置登记统一由 engine/shader 的构建描述生成，记录逻辑名、源码、Pass、用途与部署子目录；项目材质源由 parser 自动发现。全量动作编译全部内置登记/项目发现源的当前 Vulkan ES3.1 配置，收集源码 default、已保存材质继承前缀、窗口草稿及设置中的额外 typed 配置；声明身份由 parser 校验，按源与 policy 展开 roles/factories/engine selections，不枚举全域笛卡尔积。默认 permutation 必须由 compiler 的 typed domain/default selection 解析；例如 Unlit 的 USE_VERTEX_COLOR=false，不能用空 domain key 或首个缓存项代替。Include 作为依赖验证并参与构建失效。HitProxy 登记及候选发布已归入 MeshPass，与 ShadowDepth 各自完整验证所需 role + Local/GPUSkin。

Editor 独占编译队列一次一个任务，复用 Core Process/Thread；失败继续下一项，取消保留已提交版本。材质逐项预检和发布；Tonemap/ImGui 完整候选作为一组预检后在 RT 帧边界接管，ShadowDepth/HitProxy 集合分别预检与接管，失败清除暂存候选并保留原 owner。管线采用候选 Shader 的 Pass state；ImGui/Tonemap 没有 depth attachment，启用 depth/stencil 必须拒绝。C++ generated ABI 不兼容时要求重建程序；旧 GPU refs 按原提交生命周期保活。

Saved 恢复先验证源码/include/产物；无效时验证部署版本，有有效回退才降为 Warning。Program 可用性与最近编译结果分开；真实编译/读取/验证错误保留具体诊断，不能把所有失败归为源码变化。Tools → Shaders 提供重编译/取消；右下角消息卡片展示结构化阶段、进度、结果，所有诊断同时进入 Console 与文件。

Tools → Create → Shader 创建项目 Material Shader，模板来自现有 Unlit/Phong 源码，名称限定 Project/Surface/，路径限定 project/shader 内规范相对 .shader；不开放 Global/生成 ABI 创建。创建先解析模板与新声明、拒绝重复/越界/已存在路径，以 Core CreateNew 原子发布单文件源码，再刷新发现索引；外部冲突保留源文件并诊断，不新增人工登记清单。模板使用 Standard 顶点入口，Opaque 通过已验证的 coverage contract 复用默认角色；Masked 由编译器生成三角色入口。Custom 顶点或 coverage 的一致性仍由作者负责。源码创建、编译与 Material 创建分别反馈，不因编译失败删除源码。验证在隔离目录覆盖真实 Unlit/Phong 编译、冲突/非法路径/不覆盖和重启发现。

## 项目源码自动发现

工程根由 [Runtime](runtime.md#工程与分层配置) 注入。启动、创建成功和 Recompile 前调用 compiler/shader_source_discovery.h 的 discover_shader_sources(files, root)，通过 Core FileSystem 有界扫描 shader/**/*.shader、复用 Parser 提取名称和 Pass 名称列表、诊断重名；.hlsli 只参与 include 依赖。发现器不限制名称前缀或 Pass 用途，项目材质的 Project/Surface/、Usage Material 与必须具有唯一 Forward role 规则由 Editor ShaderWorkflow::read_sources 校验。没有文件监听，批次固定快照；无工程仅处理内置源。

项目名称限定 Project/Surface/，Editor 支持含 Forward 的多 role Material 集合，显示名不限为 Forward；各 role 在同一源中最多一个实现，按声明支持 Local/GPUSkin 或单一 factory。最多 250 项、2048 目录、16 层、单源 4 MiB、总源 64 MiB；确定顺序，拒绝链接。重名的所有项、损坏/不支持的声明带路径诊断并计入批次失败，合法项继续编译。损坏的外部编辑保留已发布的旧 Program，并可定位 parser 行号；重名/删除移除查询身份。刷新按逻辑身份保留旧 Program，后续编译/恢复仍验证 source/include/hash；索引身份不代表当前源码已验证。删除/冲突项不从 Saved 复活，已有 GPU refs 保持既有提交生命周期；改声明名使旧名称引用失效，移文件不改变逻辑名。

Create Shader 是验证后单文件 CreateNew，再刷新发现索引；外部冲突明确报错并保留文件，不增加清单/两文件回滚。shader_sources.txt 已移除；Saved/编译产物的 manifest/hash 校验仍保留，builtin_shader_sources.h.in 仍表达内置用途/ABI/部署记录。测试夹具位于 editor/tests/fixtures/shader，不要求项目保留示例 Shader。发现失败路径见 editor/tests/project_tests.cpp；真实创建/编译/重启发现见 material_shader_tests.cpp。项目 C++ Game 宿主见 [Runtime](runtime.md#工程与分层配置)，它从已发布配置索引读取 permutation，再通过完整集合 reader 验证全部 entry，不扫描程序目录猜配对，也不现场编译项目源码；独立发行使用上文 cook-vulkan 部署清单；Game 启动整批验证后才能装配场景。

## 修改与验证

改语言/ABI 同批修改 parser/AST/layout/generated C++/HLSL/reflection/manifest reader/真实示例；确认版本和内容身份变化。Vulkan 验 explicit offset、reflection 与 spirv-val --target-env vulkan1.1；后端 slot 不成为通用语义。

测试 tools/shader_compiler/tests/frontend_tests.cpp、layout_tests.cpp、compile_tests.cpp；runtime/tests/shader_parameters_tests.cpp、generated_shader_parameters_compile_tests.cpp、shader_map_tests.cpp、shader_map_entry_loader_tests.cpp、global_shader_map_tests.cpp、shader_graphics_state_tests.cpp。构建 target 从 CMake 查，失败/非法布局/缓存篡改和跨层验证比复制 happy-path 更重要。

Shader Texture2D Property 默认要求 Color，可在默认值后追加 `Usage LinearData` 或 `Usage Normal`；要求进入参数 schema format 4、schema identity 与逻辑 hash。材质赋值和候选验证检查真实资源 usage，不从参数名称或仅 UNorm 格式猜测用途。

Stage 编译请求 version 3 按 stage 的源、宏、依赖、ABI 和实际 native binding 计算。一次 source job 共享已经过 DXC/spirv-val 的字节码，并对每次复用重新执行当前 Program 的反射校验。ShaderMapEntry version 10 的 stage manifest 保存独立 ABI hash；SPIR-V 存于 `stage_code/<binary_sha256>/code.spv`，读取时验证目录、大小、hash 与完整 Program mapping。只修改 PS 输入不会迫使无差异的 VS 重编译；改变共用常量布局或 include 内容仍会改变相应身份。

## 运行与迁移边界

内置表面 Shader 与项目 Shader 共用发现、编译、加载、参数绑定及候选发布入口；算法属于 Shader 作者，帧调度/附件/绑定协议属于引擎。使用 Forward、一个选定方向光和最多四个点光。PBR 没有球谐、环境漫反射、BRDF LUT、质量维度、透明排序、Deferred、动态反射探针或 Shader Graph。

只接受本次 contract 的新格式，不提供旧模式解析、自动迁移、兼容开关或双轨运行。仓库受控输入已重建；外部旧 Shader 源需显式改写，改变的资产离线重新导入/重建，Shader 产物整体重编。失败候选保留当前有效 revision 是事务回滚，不是旧格式兼容。当前版本为语言 2、ABI 3、generated schema 4、domain 2、entry 10、stage request 3、index 5、deployment 1；资产版本见 Assets/Animation 主文档。

Source/domain/资产与 ShaderMap revision 不可变；GT 持 Material/Library/草稿，RT 持 Proxy/cache。worker 仅生产 owned 候选，GT 接管后经 FIFO 验证/发布。静态选项、父材质、source/policy 改动必须完整候选，验证每个实际用户自己的配置和几何能力后统一接管；任一失败保留旧图/Proxy/参数/效果。普通参数无需编译。详细事务与 binding cache 见 [Material](material.md)。

## 内置源码与用户权限

“内置”表示随引擎提供；是否用户可选择由用途决定，与文件所在根无关。项目不得覆盖保留的 Toy3d 名称，Engine 源在资产编辑器中只读；通过项目副本创建不同身份进行扩展。

| 类别 / 逻辑身份 | 提供方与用户用途 | 所需程序及条件 |
| --- | --- | --- |
| `Toy3d/Surface/Unlit` | 引擎提供的普通 Material Shader；用户可选、创建实例、修改参数/静态选项或复制源。 | Forward；Local/GPUSkin；不接受灯光、阴影接收或 IBL 配置；可以作为阴影 caster。 |
| `Toy3d/Surface/Phong` | 普通 Material Shader，同上；提供经典 Phong 简单光照模型，按新协议重写，不承担旧模式兼容。 | Forward；Local/GPUSkin；`USE_LIGHTING` 静态开关；阴影接收按 Pass 配置；源定义 ambient/specular 参数及经典点光衰减。 |
| `Toy3d/Surface/PBR` | 新增的普通 Material Shader，同上；metallic/roughness 工作流，统一采用 UE Mobile 低成本 BRDF。 | Forward；Local/GPUSkin；`USE_LIGHTING`、法线/打包数据/自发光贴图等声明选项；直接光、阴影及可选天空镜面 IBL。 |
| `Toy3d/ShadowDepth/Default` | 引擎控制的 MeshPass Shader，不出现在创建材质列表。 | ShadowDepth；Local/GPUSkin；只用于已验证标准几何且不需要透明裁剪的默认深度路径。 |
| `Toy3d/Editor/HitProxy` | 引擎控制的 MeshPass Shader，用途为 MeshPass。 | HitProxy；Local/GPUSkin；相同默认复用条件；写 R32UInt ID。Player 构建不包含。 |
| `Toy3d/PostProcess/Tonemap` | 引擎控制的 Global Shader，不是表面材质。 | 无 VertexFactory；HDR 到当前输出颜色约定；仅编译登记的输出配置。 |
| `Toy3d/UI/ImGui` | 引擎控制的 Global Shader，不是表面材质。 | 无 VertexFactory；消费当前 UI 数据和纹理协议。 |
| 公共 `ToyMeshVertex`、`ToyGPUSkin`、`ToyLighting`、`ToyShadow`、`ToyBRDF`、`ToyPBR` includes | 共享源码；项目通过白名单虚拟路径使用。 | 代码复用、标准数据语义；骨骼仍为 typed buffer。include 本身不建立 Material 或独立运行时 Program。 |

不为了 PBR 添加环境预过滤 compute Global Shader：首版离线 CPU 生成环境资产。后续确有 GPU 工具需求再接入现有编译机制。

用户态可以定义 Material Properties、声明 bool/enum 静态选项、写着色公式、读取已接受的灯光/阴影数据、提供标准 mesh roles 下的 VS/PS、改变允许的 Pass state。用户 HLSL 不得重定义保留宏、绕 schema 声明 native binding、占用任意 Global/View/Object 资源或建立 submit/present。项目 Native 代码需要不同资源或调度时，由 composition root 显式接入对应业务 Pass；不因一个 .shader 文件自动注册新的渲染阶段。

## Pass 协议与顶点行为

Pass 的源码名字与语义 role 分开。首版 Material roles 为 Forward、ShadowDepth、HitProxy；同一源对每个 role 最多一个实现，重复或未知 role 拒绝。每个 role 定义附件、输出与允许的 state/resources：Forward 写线性 HDR，ShadowDepth 写 reversed-Z 深度并采用引擎 caster bias，HitProxy 写选中 ID 与同姿态深度。Renderer 按 role 选择，源的显示名称不参与猜测。Tonemap/UI 沿各自 Global contract 管理。

标准几何与完全自定义几何明确分开：

- Standard：使用编译器指定的公共 mesh vertex 入口；引擎负责标准位置/法线/切线变换及 Local/GPUSkin，用户可以完整编写 PS。标准入口输出已定义 varyings，用户 PS 可消费兼容子集。此模式不能自定义 VS entry point，所以默认 Pass 复用有可检查的依据。
- Custom：用户自定义 VS/varyings 或顶点位移，显式声明支持的 VertexFactory，自行调用公开 helper；允许控制深度测试、深度写入和自定义 fragment depth 输出。作者提供所需 ShadowDepth/HitProxy role，并负责其位移、蒙皮、coverage 与深度行为；不能沿用标准默认几何。引擎继续控制附件、调度、资源协议与各 role 的输出类型。用户只提供 Forward 时仍可用于不投影且不拾取的用途；要求缺失 role 的赋值/功能启用预检失败。首版仍只支持 Opaque/Masked，不开放透明混合或任意帧调度。

内置 Unlit/Phong/PBR 使用 Standard 几何。Opaque 的 ShadowDepth/HitProxy 集合引用共享引擎默认程序；Masked 使用该表面源的 mask 函数与材质纹理生成对应程序。公共函数共享 alpha 判定，三种 Pass 使用同一个阈值，不允许仅 Forward discard。Custom 即使当前无位移，也不靠字符串检查或作者声明“无位移”自动取得 Standard 的默认复用资格。

Standard 的默认复用还要求 coverage 合约：Opaque 的用户 PS 及依赖不得 discard/clip、输出自定义深度或启用 alpha-to-coverage，编译后验证禁止的 fragment 行为与输出。Masked 必须提供共享 coverage 函数，编译器生成三种 role 的入口包装并调用它，用户 Forward 着色函数不能追加另一套 discard/深度逻辑。coverage 只能依赖公共 UV/颜色、Material 参数/纹理及标准 Object 数据，不能依赖仅 Forward 可见的灯光或屏幕颜色。需要这些自由度时选择 Custom 并提供所需 Pass；不能仅凭 Geometry=Standard 就保证所有轮廓一致。

Source 显式声明适配范围与已接受 engine features，compiler 根据解析后的声明和 include 依赖验证，不继续搜索原始 include 字符串。标准 schema/varyings 由编译器和公共 include 提供；Material 所有 Properties 的 full schema 在其变种/Pass 间保持稳定，active layout 可以不同。Pass 的参数 schema 按 role 定义，不能要求 Forward、ShadowDepth、HitProxy 拥有相同 Pass 参数。

Standard 的函数签名、公共输入字段和包装入口以 v2 EBNF、ToySurface.hlsli 与真实内置示例为准。验证区分接口/资源检查与可检测的字节码行为，不宣称 reflection 可证明任意 HLSL 的语义。共享 coverage 表达同一裁剪策略，不保证不同投影、采样 LOD 的逐像素结果相等。

Standard 接口固定为 `float4 shade(ToySurfaceInput input)`，Forward HLSLPS 的 `#pragma pixel` 指定该函数；`CoverageFunction coverage` 指定 `float coverage(ToySurfaceInput input)`，返回有符号 coverage，负值裁剪。`SURFACE_MODE : enum { Opaque, Masked }` 选择标准表面模式，未声明时为 Opaque。公共输入包括 world_position、world_normal、world_tangent、world_bitangent、uv、color、front_face；函数不输出 depth 或自行 discard。Coverage 函数定义在 HLSLINCLUDE，使用公共 UV/颜色、Material/Object，禁止 View/Forward Pass 依赖。着色与 coverage 分别编译无裁剪包装，检查 SPIR-V fragment discard/depth/sample 输出；coverage 的 reflection 仅允许 Material/Object。编译器生成所有入口；Masked 自动生成 ShadowDepth/HitProxy，Opaque 从已验证的 Standard contract 复用引擎默认集合。VS/varyings 与背面 frame 由公共 ToySurface include 管理，切线要求随 NormalMap 开启验证。

## 组合规划与引擎选项

`plan_shader_compilation` 是 CLI、Editor、索引 admission 共用的 Core 规划入口。Material bool/enum 静态配置、engine Pass selection、VertexFactory、target/profile 各有独立身份，不向 Material 资产暴露引擎宏。Features/SupportedWhen/GeometryRequirements 使用 Equal、Profile、Capability、Not、All、Any 的有界条件；最多 64 postfix 节点，先验证所有分支，不允许短路隐藏非法名称。

Forward 的 `SHADOW_MODE={Off,PCF}`、`ENVIRONMENT_MODE={Off,Sky}` 只加入声明相应 feature 且该配置启用它的源。接收阴影由 feature、Primitive receives_shadows、View 的有效方向光阴影及 policy 共同选择；环境由 feature、policy 和有效场景资源选择。配置但损坏的资源明确失败，不能当 Off。CastShadows 是 CPU caster 过滤；two_sided 改 Cull 和标准背面 frame。灯光数、cascade 数、骨骼影响数均为运行时数据。

配置来自源码 default、Material/Instance 保存继承图每段前缀、Editor 草稿和 build settings 的额外 typed 配置。动态代码计划使用的配置必须显式加入设置。Standard Opaque 的深度/拾取复用默认 MeshPass；Masked 只把 coverage/顶点相关选项投影到对应角色。Player 排除 HitProxy。每源最多 1024 Program、整批 4096，32 typed 维度、每 enum 32 值；预算先检查再展开，超限失败。没有压缩库、draw 内发现/编译、最近配置匹配或 Local 替代 GPUSkin。

普通 PBR 着色开关只影响 Forward PS；USE_VERTEX_COLOR 改变输入及着色，声明 Vertex/Pixel；SURFACE_MODE 影响各 coverage PS。Local/GPUSkin 仅改变标准 VS，Forward shadow/environment 只影响 PS。共享 include 的宏依赖进入校验和身份；token paste 与重定义保留宏拒绝。

## 属性范围与默认纹理

`Range` 的 finite binary32 bounds 保存在公共 schema（generated format 4），Player 不依赖 EditorProperties。编译器检查范围/default；资产、Material 创建和整批 setter 拒绝越界，不静默截断，两端包含。范围/default/UI 改变 schema identity，不改变 GPU packing、logical layout 或 stage 代码身份；Editor Range 元数据必须与公共 bounds 一致。

内置 Texture default 的名称/身份/Usage 由 Core `builtin_texture_assets.h` 统一声明。`resolve_builtin_material_texture_defaults` 从 Engine 资产加载缺失的 white/black/brick/normal_flat/white_linear，保存在调用方 MaterialTextureValues；失败不发布部分结果，不建立全局缓存。项目自定义 default 由 composition root 提供。默认值可用于缺省资源；显式损坏引用始终报错。Toy3dDefaultAssets 离线生成线性默认纹理、E_Studio GGX 环境、E_PreviewCourtyard（源 HDR 位于 asset_pipeline/source，面大小 256）和有效切线的 S_MaterialPreview 球体，使用既有 AssetPairStore 发布。

## PBR 的具体算法

算法依据核对过的 UE 5.5.3 的 `Engine/Shaders/Private/MobileGGX.ush`、`ShadingModels.ush::MobileSpecularGGXInner/GetEnvBRDF`、`BRDF.ush`、`ShadingCommon.ush`，以及 `MobileBasePassPixelShader.usf` 的 roughness 下界；不是 UE4.27 源码逐行验证。首版统一采用 Mobile 的 low-quality isotropic metallic/roughness 分支，即 `MobileSpecularGGXInner` 的 bHighQualityBRDF=false、GetEnvBRDF 的分析近似分支；不新增桌面/手机或高低 BRDF 质量维度。UE5.5 中的 Substrate、面积光、各向异性及附加多次散射能量补偿不在首版范围，不能宣称移植整个 Mobile renderer。公共参数见 [Epic 的 PBR 输入说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials?application_version=4.27)，简化思路见 [Epic Mobile PBR 文章](https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile)；该 2014 文章中的方向光 D_Approx 是历史近似，不与已核对版本的 D_GGX_Mobile 混用。

算法 contract 固定如下，实现用 Toy3d 自己的 helper 与 CPU reference 进行数值核对：

- BaseColor 为线性 RGB；Metallic/Roughness/Specular 在 [0,1]，Specular 默认 0.5。DiffuseColor = BaseColor × (1 − Metallic)，F0 = lerp(0.08 × Specular, BaseColor, Metallic)。金属度 1 没有漫反射。
- roughness 用感知参数；求值 r=max(roughness,0.015625)，与已核对的 Mobile BasePass 下界一致，资产值仍可保存 0。alpha=r²；Mobile GGX 采用 p=alpha/(1−NoH²+(NoH×alpha)²)、D=min(p²/π,2048)。NoH clamp 到 [0,1]，NoL≤0 直接返回零；NoV、half-vector 退化用具名安全规则处理。
- 直接光 specularBRDF = D × (0.25×r+0.25) × EnvBRDFApprox(F0,r,NoV)，对应 Mobile 低成本的 visibility 与 Fresnel/BRDF 联合近似；直接光 diffuseBRDF=DiffuseColor/π。两项相加后乘 NoL、光强、衰减及阴影可见性；不额外求 Smith/Schlick，不再次除 4NoLNoV，也不采用 roughness+1 hotness 重映射。EnvBRDFApprox 的 AB 拟合及 F90=saturate(50×F0.g) 对照源码，F0=0 时反射为零。
- 方向光和现有点光使用同一简化 BRDF，保留当前灯光数量范围，不照搬历史 ES2 renderer 仅动态计算太阳光的限制。结果加 emissive；AO 首版只调制环境镜面 IBL，不压暗直接光或自发光。没有采用 UE5 能量补偿时，测试按所选近似 contract 判断，不声称完整 DefaultLit 的能量补偿结果。
- 镜面 IBL 使用离线 GGX 预过滤 cubemap × EnvBRDFApprox(F0,r,NoV)，每像素一个环境 Cube 采样，不使用 BRDF LUT。分析 BRDF 只依赖材质、NoV，可在像素内复用于多盏直接光与环境路径，Environment=Off 时直接光仍需要它。没有有效环境时选择 Off；首版没有球谐、irradiance Cube 或固定 ambient 补偿，漫反射仅来自直接光，无灯区域的非金属漫反射会较暗。

简化算法首版用 float 运算保持 Vulkan ES3.1/D3D11 基线，不把源码 half 写法等同于要求原生 16-bit arithmetic。保留 Mobile D 的 2048 上界与退化保护；是否降低计算精度只能在能力与误差证据齐全后单独决定。Metallic/Specular 仍是可变参数，所以不根据缺省值启用 NONMETAL 专用近似；FULLY_ROUGH 与 high-quality BRDF 也不加入首版，避免额外选项及错误的常量假设。

PBR Properties 首版为 base_color/base_color_texture、metallic、roughness、specular、normal_texture/normal_scale、metallic_roughness_occlusion_texture、occlusion_strength、emissive_color/emissive_texture、uv_scale 与 Masked 的 alpha_cutoff。BaseColor/Emissive 颜色贴图为 sRGB；法线与打包数据为线性。打包纹理固定 R=AO、G=Roughness、B=Metallic，乘对应标量；默认白数据纹理得到 AO=1、粗糙度/金属度由标量决定。默认 metallic=0、roughness=0.5、specular=0.5、emissive=0。

PBR 静态开关为 USE_LIGHTING、USE_NORMAL_MAP、USE_MRO_MAP、USE_EMISSIVE_MAP、USE_VERTEX_COLOR、SURFACE_MODE={Opaque,Masked}；BaseColor texture 默认白并常驻，不为每张颜色纹理再加 bool。emissive 颜色/强度与 metallic/roughness 标量均为运行时参数。PBR lighting 关闭时法线/MRO 不参与 Forward active layout，保留 color、alpha、emissive 行为；用户修改这些未使用参数不强行触发另一套程序。

灯光强度首版沿 Toy3d 的相对辐射尺度，不宣称已经是 lux/lumen。PBR 点光采用以米计算的有限 inverse-square helper：d = distance_cm × 0.01、r = range_cm × 0.01；range≤0 不贡献光，其他情况 attenuation = saturate(1 − (d/r)⁴)² / max(d², 0.0001)，近场下界为 1 cm，灯光中心退化方向安全处理。该截断与下界是 Toy3d 的点光策略，不假称完整复制 UE 灯光单位/衰减系统；BRDF 与衰减分别核对。Phong 使用其源定义的经典衰减，两者读取相同灯光数据，算法选择是 Shader 内容，不保留旧材质模式分支，不改现有灯光注册/选灯策略。

## PBR 必需资产与几何接入

Texture2DAsset schema 2 保存 usage（Color、LinearData、Normal）及 import 设置：Color 在线性空间滤波后编码 sRGB，LinearData 按数据滤波，Normal 解码向量、滤波并归一化。runtime view 的色彩格式从已验证资产确定，不按绑定槽猜测或对同一 cooked asset 偷换格式。新资产必须显式记录 usage；旧格式拒绝并要求重新导入，不自动补成 Color。改变 usage 要重新导入，材质要求不兼容格式时提示具体资源。

usage 同时进入 cooked metadata 与 Shader sampled-property 要求；Color 使用 RGBA8 sRGB，LinearData/Normal 使用 RGBA8 UNorm。不能仅凭 UNorm 区分 Normal 与普通线性数据，也不根据变量名称猜要求。首版 normal map 固定 tangent-space XYZ 编码 [0,1]→[-1,1]、正 Y 约定；导入选项允许翻转绿色通道并记录，normal_scale 缩放 XY 后重新归一化。

EnvironmentAssetData：仅包含版本化 GGX 预过滤 Cube payload，不设置球谐系数或预留对应占位数据。CPU 导入/烘焙留 Toy3dAssetPipeline，DTO/验证留 Core Assets。首版支持有界 Radiance HDR panorama，复用现有图像基础设施扩展 float decode，不顺带承诺 EXR。Cube face 方向、左手坐标、mip 粗糙度及数据布局是资产 contract；格式 R16G16B16A16Float、完整六 faces/mip、有限非负 HDR 值，Cube 大小不超过 512，源/解码/烘焙均检查预算。未知算法版本或 orientation 拒绝，不猜测匹配。

首版 mip 粗糙度为 mip/(mip_count−1)，采样 LOD 为求值 r × (mip_count−1)；mip 0 是未模糊的环境，后续用 N=V 的 GGX importance sampling 预过滤，积分权重按 NoL 归一化。Cube 顺序固定 +X/−X/+Y/−Y/+Z/−Z；以 s=2u−1、t=2v−1、纹理 v 向下，对应方向分别为 (1,−t,−s)、(−1,−t,s)、(s,1,t)、(s,−1,−t)、(s,−t,1)、(−s,−t,−1)，归一化后采样。panorama 的 u=atan2(x,z)/(2π)+0.5、v=acos(y)/π；导入与运行时不额外交换 Y/Z。

Runtime 为场景提供一个 owned environment snapshot（Cube 资源、旋转与强度），GT 通过现有 World/settings 与 SceneInterface 同 FIFO 发布，RT 负责资源；Scene 资产增加可选环境引用，新格式场景未配置环境时为 Off，旧格式要求按新结构重建。窗口预览与缩略图使用独立环境和灯光，配置见 [Material](material.md#可视预览与缩略图)，不污染主场景；通过直接光照亮非金属表面，不暗中增加球谐或假环境漫反射。默认环境由离线工具生成并随引擎部署；引擎与项目环境共用资产加载。首版没有动态 probe 或 per-object environment，所以不为每个 Material 增加 Cube 属性及不同环境身份变体。

法线贴图增加 Tangent0：StaticMesh/SkeletalMesh 共享切线方向与 handedness，导入/构建按 UV seams/hard edges 生成或验证，缺有效 UV/tangent 的几何不能静默启用 normal map。Tools 统一采用 [MikkTSpace 官方实现](https://github.com/mmikk/MikkTSpace)，不能把自写近似命名为 MikkTSpace。作为 Tools 的固定 revision 构建依赖，源码缓存放根 build 下并支持离线指定已核对源，不改 engine/thirdparty，不让 Runtime 依赖生成器。构建时不跟随浮动 master，也不因缺依赖退回另一算法。旧 mesh payload 不由 Runtime 补切线或兼容读取，要求离线从源重建新格式，不能在 draw 热路径生成。

MikkTSpace 按 corner 生成结果，拆分顶点必须同步复制颜色、所有 UV、骨骼索引/权重及其他顶点属性并重建 indices。资产显式记录有效切线状态；NormalMap=Off 可使用已初始化的安全切线，启用时必须具备有效 UV/切线。环境 Cube 大小限定 2..512 的二次幂并包含完整 mip 链；源/烘焙结果不能表示为有限非负 FP16 时导入失败，不静默截断。材质的数值范围由 schema 与编辑/加载边界共同验证。

蒙皮位置/切线按线性变换 rows，法线按已有 inverse-transpose normal rows；随后 Gram-Schmidt 正交化切线并用 handedness 重建 bitangent。Object 非均匀 scale 同样处理；退化 frame 报告并使用具名安全法线，不能产生 NaN。重用现有六 Float4/bone typed 数据，不新增骨骼 StructuredBuffer，不改变正 scale 与 4/8 共用 Shader contract。切线和新的法线约定要覆盖 static、GPUSkin、镜像 UV 和三种 Pass。

PBR 不在材质 Shader 内做 tonemap/gamma；仍输出线性 HDR，经当前 Tonemap/UI 路径。新增 HDR 纹理/Cube 时逐 profile 检查 sampled/filter/limits support；Vulkan ES3.1 不因桌面能力抬高 SPIR-V 版本、descriptor set 数或可选能力要求。能力不足明确不支持该环境配置，不能上层判断 Vk/D3D 后偷改算法。

## 标准表面切线与几何要求

`SurfaceInputs { Tangent }` 在整个 Standard 源中固定开启 `TANGENT0` 及切线插值，和某个配置是否采样法线贴图区分。`GeometryRequirements { TangentFrame When All(Equal(USE_LIGHTING, true), Equal(USE_NORMAL_MAP, true)) }` 声明当前静态配置要求有效切线；Custom 也可声明几何要求。该有界条件与 domain/policy 一起进入索引，GT 赋材质及 RT 候选发布检查对应用户网格的不可变能力。要求有效切线的配置拒绝缺少有效 UV/tangent 的网格，关闭相关选项时可使用初始化的安全切线。Renderer 不根据 Shader 名字或选项名推测要求。

## 使用方式

操作流程与实际查询入口如下：

1. 普通材质：选择 Unlit/Phong/PBR 或项目 Shader，创建 Material，设置颜色/标量/贴图及本地静态选项；创建 MaterialInstance 时可覆盖这两类值，赋给 StaticMesh/SkeletalMesh 的材质槽。纹理导入时选择 Color/LinearData/Normal，PBR 的 MRO 固定 R=AO、G=Roughness、B=Metallic。
2. 场景控制：Primitive 设置投影/接收阴影，场景设置镜面环境资源、强度与旋转；用户不手选 Local/GPUSkin 或 Off/PCF/Sky 程序。光照开关仅显示在声明它的 Shader 上，two_sided 改 Cull 与标准背面处理，不额外生成变体。
3. 自定义 Shader：在 project/shader 创建新语言源，声明 Properties、Variants、roles、factory/feature 支持及 stage 影响；Standard 编写着色/coverage 函数并使用标准入口，Custom 自写 VS/PS 及所需阴影/拾取 Pass。Engine 的 Global/default MeshPass 不出现在创建材质列表。
4. 编译与发布：Editor 修改静态选项后生成并预检完整候选，成功后一次发布；普通参数变化只更新数据。发行构建从资产与 policy 收集配置，额外动态配置要显式列入 policy；运行时只切换已部署配置，缺失时报错且保留当前有效状态。
5. 渲染查询：从 MaterialRenderProxy 的已发布 ShaderMapCollection，按 role + 实际 VertexFactory + Pass selection 取得 Program，再按 active layout 准备 bindings/pipeline。标准 Opaque 的默认深度/拾取结果可引用引擎共享集合；Masked/Custom 按已验证源的 role 取得程序，不扫描缓存或派生 skin key。

例如，同一 PBR 材质赋给静态网格和骨骼网格时复用相同 Material 配置，分别查询 Local/GPUSkin；开关接收阴影只改变 Forward 的 Pass selection，颜色/roughness 变化只改参数，USE_NORMAL_MAP 变化需要另一个静态配置。所需引擎组合均预先编译，用户不管理它们的宏。

## 参考依据

- [Unity URP Pass tags](https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/manual/urp-shaders/urp-shaderlab-pass-tags.html)：借用 role 协议与自定义 Shader 接入思路，不借用 URP 标签文本作为 Toy3d API。
- [Unity 官方 URP Lit 源码](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/Lit.shader)：内置材质通过同类 Pass、Material keywords 与 pipeline keywords 组合的依据，不照搬 Unity 全局 keyword 状态。
- [UE4.27 Shader Development](https://dev.epicgames.com/documentation/en-us/unreal-engine/shader-development?application_version=4.27)：Material/VertexFactory/ShaderType 组合、支持条件过滤与默认深度材质复用的职责依据。Toy3d 仍以 role 对应的完整 Program 为查询单位。
- UE 5.5.3 源码核对入口：`MobileGGX.ush::D_GGX_Mobile`、`ShadingModels.ush::MobileSpecularGGXInner/GetEnvBRDF` 的 low-quality/分析近似分支、`BRDF.ush::EnvBRDFApprox/EnvBRDFApproxLazarov`、`MobileBasePassPixelShader.usf` 的 roughness 下界、`ShadingCommon.ush::DielectricSpecularToF0/ComputeF0`、`DeferredShadingCommon.ush` 的 DiffuseColor 计算，及 `MaterialShared.h::FMeshMaterialShaderMap/GetMeshShaderMap/GetShader`。版本与历史 Mobile 文章分别记录，不能把不同年代的直接光算法混成同一个实现。
