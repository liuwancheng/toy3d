# Shader：语言、ABI、编译与运行时

## 定位与当前范围

tools/shader_compiler 为 frontend/layout/codegen/CLI，rendercore/shader 为 ShaderMap/Loader/Program/typed parameters，engine/shader 与 project/shader 是源码。Toy3dShaderFormat 是独立格式库，源码位于 engine/core/shader，依赖 Core，不依赖 Runtime 或 compiler；engine/shader 只保存 shader 源码和构建规则。

Shader logical schema 是权威；reflection 验证字节码并提供 native mapping，不能反向补出未声明业务参数。RHI 不解析 .shader、C++ generated metadata 或 Material 属性。

当前生产外部编译入口 compile-vulkan；D3D11 FXC/SM5、D3D12 DXC 的工具链设计不等于已完成外部编译或后端。Cook/Shipping ShaderCodeLibrary 的只读去重发布仍为演进范围，不能虚构完整 cooker/loader。Editor 消费集合索引及其引用的已验证 loose ShaderMapEntry。

## 语言与源码边界

语言只接受 Version 2，旧源明确失败并要求按新协议重写；packing/默认语义变更仍须提升 language/ABI major，未知关键词错误。

语法权威 [shader-language-v2.ebnf](shader-language-v2.ebnf)，可用真实示例 engine/editor/tests/fixtures/shader/painted.shader 与 engine/shader 内置文件，不写想象的 DSL。源码声明 Usage=Global/Material/MeshPass；Material/MeshPass 当前使用 Geometry Custom 并显式列出 VertexFactories，Global 没有 mesh geometry。每个 Pass 的显示名与 Role 分开，独立 HLSLVS/HLSLPS/HLSLCS 块各声明一个匹配的 stage pragma，HLSLINCLUDE 不声明入口。Forward/HitProxy 必须有 VS/PS，ShadowDepth 允许只含 VS，compute 仅属于 Global。当前尚未实现 Standard 包装、engine feature 声明及 stage 影响语法，不能据此假称完成整个统一材质方案。

- Properties 是 Material 属性，保留源码顺序/typed default/UI；不是任意 HLSL struct。当前不开放属性数组，矩阵为 Matrix4x4。
- Parameters 首批只 Pass group 的 Float/Float2/Float3/Float4/Float4x4；省略 default 全零，显式 default 分量类型/有限性一致，不开放数组/struct。
- Resources 受控 Texture2D/2DArray/3D/Cube/2DMS、Sampler/ComparisonSampler、Buffer/ByteAddressBuffer/StructuredBuffer 和 EBNF 中 RW 类型。语言识别不保证当前 backend/material 支持所有组合。
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

engine-owned Object canonical 数值包含两组 Matrix4、Float `toy_receives_shadows`（offset 128）与 UInt32 `toy_num_bone_influences`（offset 132），总大小 144 bytes；影响数由骨骼几何提供 4/8，仅 GPUSkin 使用，行为见 [Animation](animation.md)。4/8 不构成 shader permutation；数值成员进入 layout hash，旧产物须重新编译并与生成参数共同部署。

schema hash（含 default/UI/sampler preset）、logical layout hash、target binding hash、bytecode hash 分离；compile/package/dependency 内容寻址 SHA-256，parameter identity 保持 FNV。

## Variant/permutation

VariantId/EnumValueId 用带 little-endian uint32 长度前缀的 FNV domain；variant name 与 enum option 的稳定身份不随声明排序变更。0 invalid，rename 新身份，kind 改变进入 permutation record。

compiler 按 schema 补 defaults，拒绝未知/重复/非法选择和碰撞；按 VariantId 排序，序列化版本/count/ID/kind 与 bool uint32 0/1 或 EnumValueId，SHA-256 得 permutation key；空 domain 也有非零带版本 key。

规范化实现位于 Toy3dShaderFormat 的 `shader/shader_permutation.h`：共享 domain 与 typed selection 不依赖 AST/Runtime，Tools 的 `compiler/variant_permutation` 只转换 AST/CLI 文本并补源码位置诊断。Material/Pass scope 分离 ID 与 generated 宏；每域最多 32 维、每 enum 最多 32 值。默认值、显式 kind、孤儿/重复选择、非法声明及宏碰撞必须整体校验后才能发布结果。共享入口支持按单个 stage 投影并先验证完整配置；当前语言尚无 stage 声明，生产 compiler 仍保守使用完整域，不宣称已实现 VS/PS 编译复用。独立 Core 验证入口为 `Toy3dShaderFormat.Permutation`，只依赖 Toy3dShaderFormat。

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

mesh role 的 Pass 参数由 compiler 统一注入：ShadowDepth 使用 shadow_world_to_clip/shadow_light_direction/shadow_bias_parameters，HitProxy 使用 hit_proxy_id_parts；Material/MeshPass 源码根 Parameters/Resources 的 Pass 声明描述 Forward，不能改写这两个角色 ABI。Global 的源码参数保持既有规则。C++ codegen 按各 Pass 实际 role 生成参数类型，不把 Forward schema 复制给其他角色。

`validate_shader_parameters_metadata_against_schema` 继续验证完整 schema 身份；`validate_shader_parameters_group_against_schema` 验证版本与完整单组 ABI，供引擎 Pass group 跨材质源复用。Custom 的 Material/其他组可以不同，不能因此跳过 Pass 字段/资源类型、数量、offset/stride/default 的校验。

## ShaderMapEntry 与缓存

目录按调用方期望 shader_map_key 定位，reader 重算身份，不相信 manifest 自报 key/扫描猜测。required 文件：manifest.txt、mapping.txt，每 stage 的 manifest/spv/reflection/dependencies；vertex、vertex+pixel、compute 组合需 stage/entry/mapping/reflection 一致。

entry format 7 持久化并验证 Usage、Role、Geometry、所选 VertexFactory 及源的 factory 支持范围，这些字段均进入内容身份。Program 查询缓存分别比较 role/factory/static key，Local 与 GPUSkin 不再共享同一查询身份。compile-request 为版本 2；reader 只读新格式，旧 entry 重新编译。Runtime 与 Editor 必须读取集合索引，不扫描目录寻找程序；没有索引的旧产物需要重编译。

- UTF-8 LF、唯一 key=value、版本定界 record；未知/重复/缺字段、非 canonical 数字/小写摘要、非法 flags/enums/paths、重复 binding/stage、未排序 dependencies 拒绝。
- manifest ≤64 KiB，metadata 单文件 ≤4 MiB，stage binary ≤64 MiB，各类 record ≤4096。先限大小/hash，再解析/重算 semantic hash，最后重算 shader_map_key/entry_content_hash。
- shader_map_key 覆盖 Shader/Pass、target/profile、mapping/layout、template、Variant/permutation 与 stage compile/reflection/binary；entry_content_hash 另含具名 mapping/full dependency record，区分同 key 内容冲突。
- writer 用 owned staging/no-replace rename；已存在必须用同 reader 全验证且 key/content 相同才 cache_hit，损坏/冲突诊断、不覆盖/删 final。
- reader 只验 artifact，当前源码/include/toolchain identity 由 compile/cache 调用方验证；不存在“缓存文件在就可信”的捷径。

## 完整集合索引与候选

`shader/shader_map_index.h` 是 Core 的集合协议，index version 1 位于产物根的 `shader_maps/<configuration_key>/index.txt`。配置查询 key 覆盖逻辑 Shader 名、target/profile、已解析 Material permutation；源全文 SHA-256 独立标识 revision，不把 source revision 混入配置查询。每个编译输出根只发布该配置的一份不可变 revision；相同内容可复用，冲突保留原产物，Editor 新 revision 使用独占请求目录。

索引独立保存源码声明的 Pass 显示名/Role 列表，再记录各 Pass/VertexFactory 对应的 entry key 与 entry content hash。声明列表乘源码 factory 支持范围必须完整覆盖；Global 每个具名 Pass 对应 None，mesh role 唯一，Material 必须有 Forward。缺掉整个 Pass、缺一个 factory、重复身份、未知声明及 entry 不匹配均失败。Pass/程序各不超过 1024 项，索引不超过 4 MiB；规范排序、UTF-8 LF 与 payload 摘要同时验证，拒绝未知记录、损坏内容及链接路径。

`compile-vulkan` 一次编译同一源码的所有声明 Pass 和 factory，现有 Pass 参数用于检查请求入口是否存在；当前配置仍由 typed Material selection/defaults 决定。每个 entry 先独立完成内容寻址发布，全部通过同一 reader 验证后，索引最后用 owned staging/no-replace rename 发布。中途失败允许留下可复用的 entry，但不产生可加载的半集合索引，也不覆盖旧索引。这里只覆盖当前语言已声明的组合；engine Pass selections、required 组合规划与 stage 编译去重仍待实现。

Runtime 的 `ShaderMapEntryLoader::load_collection` 验证索引及全部 entry，失败不返回部分程序。`ShaderMapCollection::create_candidate` 再检查程序契约、完整覆盖与 schema，产生共享不可变集合；同一 Material 的全部 roles/factories 保持完整 Material schema，同 Pass 的 factories 保持 Pass schema/graphics state。GPUSkin peer 必须具有 Object 的骨骼 typed buffer 与两组 influence 输入。`find(role, factory)` 精确查询网格程序，Global 查询还须显式提供 Pass 显示名，缺项不回退。

集合内 Local/GPUSkin 是独立 Program，没有互相挂接。`ShaderMap::find_or_load_collection` 按逻辑名/profile/Material permutation 缓存不可变集合，MaterialDesc/Proxy/Library/Editor 接管整个集合。Base/Shadow/HitProxy 从实际 owner 精确查询角色与 factory；旧集合/Program refs 维持既有 GPU 生命周期。GlobalShaderMap 只接纳 Usage Global、Role Global、factory None，Tonemap/ImGui 继续用显式 GlobalShaderType 查询；HitProxy 与 ShadowDepth 由内置 MeshPass 集合持有。单 Program 的 load_program 仍供 Global/cache 等明确 entry 查询使用，先验证完整索引，但不建立其他 Program 附属关系。

## Editor 全量重编译

内置登记统一由 engine/shader 的构建描述生成，记录逻辑名、源码、Pass、用途与部署子目录；项目材质源由 parser 自动发现。全量动作编译全部内置登记/项目发现源的当前 Vulkan ES3.1/default permutation，声明身份由 parser 校验，按 VertexFactories 声明编译 Local/GPUSkin；不枚举尚未支持的全部用户 permutation。默认 permutation 必须由 compiler 的 typed domain/default selection 解析；例如 Unlit 的 USE_VERTEX_COLOR=false，不能用空 domain key 或首个缓存项代替。Include 作为依赖验证并参与构建失效。HitProxy 登记及候选发布已归入 MeshPass，与 ShadowDepth 各自完整验证所需 role + Local/GPUSkin。

Editor 独占编译队列一次一个任务，复用 Core Process/Thread；失败继续下一项，取消保留已提交版本。材质逐项预检和发布；Tonemap/ImGui 完整候选作为一组预检后在 RT 帧边界接管，ShadowDepth/HitProxy 集合分别预检与接管，失败清除暂存候选并保留原 owner。管线采用候选 Shader 的 Pass state；ImGui/Tonemap 没有 depth attachment，启用 depth/stencil 必须拒绝。C++ generated ABI 不兼容时要求重建程序；旧 GPU refs 按原提交生命周期保活。

Saved 恢复先验证源码/include/产物；无效时验证部署版本，有有效回退才降为 Warning。Program 可用性与最近编译结果分开；真实编译/读取/验证错误保留具体诊断，不能把所有失败归为源码变化。Tools → Shaders 提供重编译/取消；右下角消息卡片展示结构化阶段、进度、结果，所有诊断同时进入 Console 与文件。

Tools → Create → Shader 创建项目 Material Shader，模板来自现有 Unlit/Phong 源码，名称限定 Project/Surface/，路径限定 project/shader 内规范相对 .shader；不开放 Global/生成 ABI 创建。创建先解析模板与新声明、拒绝重复/越界/已存在路径，以 Core CreateNew 原子发布单文件源码，再刷新发现索引；外部冲突保留源文件并诊断，不新增人工登记清单。模板显式复制内置默认 ShadowDepth/HitProxy 的角色实现；作者修改 Custom 顶点或 coverage 时须同步这些实现。源码创建、编译与 Material 创建分别反馈，不因编译失败删除源码。验证在隔离目录覆盖真实 Unlit/Phong 编译、冲突/非法路径/不覆盖和重启发现。

## 项目源码自动发现

工程根由 [Runtime](runtime.md#工程与分层配置) 注入。启动、创建成功和 Recompile 前调用 compiler/shader_source_discovery.h 的 discover_shader_sources(files, root)，通过 Core FileSystem 有界扫描 shader/**/*.shader、复用 Parser 提取名称和 Pass 名称列表、诊断重名；.hlsli 只参与 include 依赖。发现器不限制名称前缀或 Pass 用途，项目材质的 Project/Surface/、Usage Material 与必须具有唯一 Forward role 规则由 Editor ShaderWorkflow::read_sources 校验。没有文件监听，批次固定快照；无工程仅处理内置源。

项目名称限定 Project/Surface/，Editor 支持含 Forward 的多 role Material 集合，显示名不限为 Forward；各 role 在同一源中最多一个实现，按声明支持 Local/GPUSkin 或单一 factory。最多 250 项、2048 目录、16 层、单源 4 MiB、总源 64 MiB；确定顺序，拒绝链接。重名的所有项、损坏/不支持的声明带路径诊断并计入批次失败，合法项继续编译。损坏的外部编辑保留已发布的旧 Program，并可定位 parser 行号；重名/删除移除查询身份。刷新按逻辑身份保留旧 Program，后续编译/恢复仍验证 source/include/hash；索引身份不代表当前源码已验证。删除/冲突项不从 Saved 复活，已有 GPU refs 保持既有提交生命周期；改声明名使旧名称引用失效，移文件不改变逻辑名。

Create Shader 是验证后单文件 CreateNew，再刷新发现索引；外部冲突明确报错并保留文件，不增加清单/两文件回滚。shader_sources.txt 已移除；Saved/编译产物的 manifest/hash 校验仍保留，builtin_shader_sources.h.in 仍表达内置用途/ABI/部署记录。测试夹具位于 editor/tests/fixtures/shader，不要求项目保留示例 Shader。发现失败路径见 editor/tests/project_tests.cpp；真实创建/编译/重启发现见 material_shader_tests.cpp。项目 C++ Game 宿主见 [Runtime](runtime.md#工程与分层配置)，它从已发布配置索引读取 permutation，再通过完整集合 reader 验证全部 entry，不扫描程序目录猜配对，也不现场编译项目源码；独立发行 Cook/Shader 打包尚未接入。

## 修改与验证

改语言/ABI 同批修改 parser/AST/layout/generated C++/HLSL/reflection/manifest reader/真实示例；确认版本和内容身份变化。Vulkan 验 explicit offset、reflection 与 spirv-val --target-env vulkan1.1；后端 slot 不成为通用语义。

测试 tools/shader_compiler/tests/frontend_tests.cpp、layout_tests.cpp、compile_tests.cpp；runtime/tests/shader_parameters_tests.cpp、generated_shader_parameters_compile_tests.cpp、shader_map_tests.cpp、shader_map_entry_loader_tests.cpp、global_shader_map_tests.cpp、shader_graphics_state_tests.cpp。构建 target 从 CMake 查，失败/非法布局/缓存篡改和跨层验证比复制 happy-path 更重要。

## 统一材质与 Shader 方案（已确认，实施中）

本节是已确认的目标 contract，按下述批次实施；正文只描述已落地能力。这里的拟定类型、声明和查询在对应实现落地前不是可用 API。实施时将已完成的 contract 合并到对应正文，最终删除本节，避免长期保留两套规范。材质持久化及实例行为归 [Material](material.md)，Pass 调度归 [Renderer](renderer.md)，资产格式归 [Assets](assets.md)，蒙皮约束归 [Animation](animation.md)；本节是此次跨模块方案的唯一入口。

### 目标与边界

使用 Unity 风格的 Shader 源定义 Material 属性、静态选项、Pass 和 HLSL；内置表面 Shader 与项目 Shader 走同一个发现、编译、加载、参数绑定、候选发布入口。引擎提供稳定的 mesh/lighting/shadow 协议及可复用函数，具体着色公式归 Shader 作者，不要求所有 Material 接受 Phong/PBR 的分类。参考 UE 的编译组合与 ShaderMap 职责，BRDF 参考实际核对的 UE 源码，不引入 UObject、宏注册或复杂模板体系。

本轮设计范围包含通用 permutation、Material 静态选项与继承、多 mesh Pass、Local/GPUSkin、光照/阴影宏、UE Mobile 简化 PBR 的直接光与可选天空镜面 IBL、线性贴图、切线、环境资产、Editor 候选与构建组合规划。球谐及环境漫反射暂缓，不生成对应数据、烘焙步骤、绑定或变体。继续使用 Forward、一盏选定方向光及现有四盏点光上限；灯光数量运行时传入，不生成数量变体。

不兼容旧模式：本轮涉及的 Shader 语言、permutation/key、Program 查询、编译产物及发生变化的资产格式只实现新 contract；不提供旧语法解析、旧格式读取、自动迁移、兼容开关、转发接口或双轨运行。仓库内置源、项目示例、测试与部署输入同批改为新格式；旧源码需显式改写，旧 cooked 资产需从源重新导入/构建，旧材质与场景按新格式重建，旧 Shader 缓存须重新编译。不删除未受本轮影响的格式支持，也不将候选失败保留当前有效版本的事务回滚误当成旧模式兼容。

非目标为 Shader Graph、任意运行时 HLSL 编译、用户 Shader 改写帧调度、Deferred/GBuffer、聚簇灯光、动态反射探针、光线追踪、清漆/各向异性/次表面/透射、布料和 compute skin cache。透明排序与透明 PBR 尚无完整 contract，本轮标准表面模式限定 Opaque/Masked，不以设置 Blend 即成功宣称透明材质已支持；自定义原生 Pass 的业务扩展仍需单独设计。

### 内置源码与用户权限

“内置”表示随引擎提供；是否用户可选择由用途决定，与文件所在根无关。项目不得覆盖保留的 Toy3d 名称，Engine 源在资产编辑器中只读；通过项目副本创建不同身份进行扩展。

| 类别 / 逻辑身份 | 提供方与用户用途 | 所需程序及条件 |
| --- | --- | --- |
| `Toy3d/Surface/Unlit` | 引擎提供的普通 Material Shader；用户可选、创建实例、修改参数/静态选项或复制源。 | Forward；Local/GPUSkin；不接受灯光、阴影接收或 IBL 配置；可以作为阴影 caster。 |
| `Toy3d/Surface/Phong` | 普通 Material Shader，同上；提供经典 Phong 简单光照模型，按新协议重写，不承担旧模式兼容。 | Forward；Local/GPUSkin；`USE_LIGHTING` 静态开关；阴影接收按 Pass 配置；源定义 ambient/specular 参数及经典点光衰减。 |
| `Toy3d/Surface/PBR` | 新增的普通 Material Shader，同上；metallic/roughness 工作流，统一采用 UE Mobile 低成本 BRDF。 | Forward；Local/GPUSkin；`USE_LIGHTING`、法线/打包数据/自发光贴图等声明选项；直接光、阴影及可选天空镜面 IBL。 |
| `Toy3d/ShadowDepth/Default` | 引擎控制的 MeshPass Shader，不出现在创建材质列表。 | ShadowDepth；Local/GPUSkin；只用于已验证标准几何且不需要透明裁剪的默认深度路径。 |
| `Toy3d/Editor/HitProxy` | 引擎控制的 MeshPass Shader，迁出当前 Global 分类。 | HitProxy；Local/GPUSkin；相同默认复用条件；写 R32UInt ID。Player 构建不包含。 |
| `Toy3d/PostProcess/Tonemap` | 引擎控制的 Global Shader，不是表面材质。 | 无 VertexFactory；HDR 到当前输出颜色约定；仅编译登记的输出配置。 |
| `Toy3d/UI/ImGui` | 引擎控制的 Global Shader，不是表面材质。 | 无 VertexFactory；消费当前 UI 数据和纹理协议。 |
| 公共 `ToyMeshVertex`、`ToyGPUSkin`、`ToyLighting`、`ToyShadow`、`ToyBRDF`、`ToyPBR` includes | 前两项已有入口，其余为拟新增共享源码；项目通过白名单虚拟路径使用。 | 代码复用、标准数据语义；骨骼仍为 typed buffer。include 本身不建立 Material 或独立运行时 Program。 |

不为了 PBR 添加环境预过滤 compute Global Shader：首版离线 CPU 生成环境资产。后续确有 GPU 工具需求再接入现有编译机制。

用户态可以定义 Material Properties、声明 bool/enum 静态选项、写着色公式、读取已接受的灯光/阴影数据、提供标准 mesh roles 下的 VS/PS、改变允许的 Pass state。用户 HLSL 不得重定义保留宏、绕 schema 声明 native binding、占用任意 Global/View/Object 资源或建立 submit/present。项目 Native 代码需要不同资源或调度时，由 composition root 显式接入对应业务 Pass；不因一个 .shader 文件自动注册新的渲染阶段。

### Pass 协议与顶点行为

Pass 的源码名字与语义 role 分开。首版 Material roles 为 Forward、ShadowDepth、HitProxy；同一源对每个 role 最多一个实现，重复或未知 role 拒绝。每个 role 定义附件、输出与允许的 state/resources：Forward 写线性 HDR，ShadowDepth 写 reversed-Z 深度并采用引擎 caster bias，HitProxy 写选中 ID 与同姿态深度。Renderer 按 role 选择，源的显示名称不参与猜测。Tonemap/UI 沿各自 Global contract 管理。

标准几何与完全自定义几何明确分开：

- Standard：使用编译器指定的公共 mesh vertex 入口；引擎负责标准位置/法线/切线变换及 Local/GPUSkin，用户可以完整编写 PS。标准入口输出已定义 varyings，用户 PS 可消费兼容子集。此模式不能自定义 VS entry point，所以默认 Pass 复用有可检查的依据。
- Custom：用户自定义 VS/varyings 或顶点位移，显式声明支持的 VertexFactory，自行调用公开 helper；允许控制深度测试、深度写入和自定义 fragment depth 输出。作者提供所需 ShadowDepth/HitProxy role，并负责其位移、蒙皮、coverage 与深度行为；不能沿用标准默认几何。引擎继续控制附件、调度、资源协议与各 role 的输出类型。用户只提供 Forward 时仍可用于不投影且不拾取的用途；要求缺失 role 的赋值/功能启用预检失败。首版仍只支持 Opaque/Masked，不开放透明混合或任意帧调度。

内置 Unlit/Phong/PBR 使用 Standard 几何。Opaque 的 ShadowDepth/HitProxy 集合引用共享引擎默认程序；Masked 使用该表面源的 mask 函数与材质纹理生成对应程序。公共函数共享 alpha 判定，三种 Pass 使用同一个阈值，不允许仅 Forward discard。Custom 即使当前无位移，也不靠字符串检查或作者声明“无位移”自动取得 Standard 的默认复用资格。

Standard 的默认复用还要求 coverage 合约：Opaque 的用户 PS 及依赖不得 discard/clip、输出自定义深度或启用 alpha-to-coverage，编译后验证禁止的 fragment 行为与输出。Masked 必须提供共享 coverage 函数，编译器生成三种 role 的入口包装并调用它，用户 Forward 着色函数不能追加另一套 discard/深度逻辑。coverage 只能依赖公共 UV/颜色、Material 参数/纹理及标准 Object 数据，不能依赖仅 Forward 可见的灯光或屏幕颜色。需要这些自由度时选择 Custom 并提供所需 Pass；不能仅凭 Geometry=Standard 就保证所有轮廓一致。

Source 显式声明适配范围与已接受 engine features，compiler 根据解析后的声明和 include 依赖验证，不继续搜索原始 include 字符串。标准 schema/varyings 由编译器和公共 include 提供；Material 所有 Properties 的 full schema 在其变种/Pass 间保持稳定，active layout 可以不同。Pass 的参数 schema 按 role 定义，不能要求 Forward、ShadowDepth、HitProxy 拥有相同 Pass 参数。

Standard 的着色/coverage 函数签名、公共输入字段和包装入口须与 v2 EBNF、最小真实示例一起固定后再实现；禁止根据拟定类型名臆造 API。验证区分接口/资源检查与可检测的字节码行为，不宣称 reflection 可证明任意 HLSL 的语义。共享 coverage 表达同一裁剪策略，不保证不同投影、采样 LOD 的逐像素结果相等。

### 声明模型与通用变体

在现有 bool/enum Variants 上扩展语言 Version 2，并同批更新 EBNF、parser、AST、codegen 和真实模板。v2 至少表示用途、Pass role、Standard/Custom 几何、支持的 VertexFactories、Material variants、接受的 engine features、各维度所影响的 Pass/stage，以及 stage 独立源块。本文只定义这些语义，不将尚未定稿的文本拼成可用 DSL；实现前先固定 v2 EBNF 与 golden 例子。

| 维度 | 权威选择者 | 身份与生成规则 |
| --- | --- | --- |
| Material 静态 bool/enum | Shader 声明，Material/MaterialInstance 选择 | 保存 typed selection；规范化后生成 `TOY3D_VARIANT_*`，与已有命名衔接。 |
| VertexFactory | 几何实际类型，源声明支持范围 | 明确的 `VertexFactoryType` 字段；生成编译环境，不覆盖 Material permutation key。 |
| Engine Pass permutation | Pass 根据已接受 feature、View/Primitive/构建 policy 推导 | engine descriptor 定义有界类型与合法值；生成保留 `TOY3D_PASS_*` 宏，Material 不能直接覆盖。 |
| Stage / entry point / target/profile | compiler 与已验证的请求 | 明确字段；源码及工具链依赖进入编译身份，不隐藏为无记录 define。 |
| 颜色、贴图内容、灯光数、姿态、4/8 影响数 | 对应数据 owner | 运行时参数，不进入 shader permutation。 |

用户自定义 Shader 不需要固定 `MaterialShadingModel` 枚举。Phong/PBR 中 `USE_LIGHTING=false` 定义为 base color 加 emissive 的无光照路径；Unlit 没有该选项。删除不再承担运行时职责的 MaterialShadingModel 分类，避免两份可冲突的着色模型状态。内置表面 Shader 是独立源，不以一个巨大 enum 强迫所有源共享所有模型。

每个 variant 定义稳定 ID/name、owner scope、bool 或有限 enum、默认值、受影响的 Pass/stages。现有 selection/ID/record 校验及规范化计算迁入 Toy3dShaderFormat 的公共 permutation 入口，源码解析仍留 Tools。用户本地维度与 engine 维度分域，源间同名开关不产生全局 keyword 状态。拒绝未知/重复值、ID/macro collision、无效 enum 和未经记录的外部 defines。

源的静态支持条件采用有界声明表达式，只允许已声明 bool/enum 的比较、逻辑组合和明确的平台能力要求；无脚本、无运行时值、无循环。compiler 与 runtime 共用解析后的不可变记录及求值规则。引擎业务约束通过普通 `should_compile_permutation(context)` 函数组合，不使用不可替换全局注册表。

区分“非法”与“无差异”：非法组合直接拒绝；声明为不影响该 Pass/stage 的维度允许投影掉，使同一结果复用。若用户输入非默认的未支持功能，报告不支持；Renderer 不给未接受该功能的 Shader 附加那一维，不能吞掉拼错的选项。

### 光照、阴影和环境的首批选择

首批 engine domains 为 Forward 的 `SHADOW_MODE={Off,PCF}` 与 `ENVIRONMENT_MODE={Off,Sky}`。PCF 对应现有方向光 cascade atlas，不包含点光阴影；Sky 对应单一场景环境，首版 PBR 只使用其镜面反射。只给显式接受相应 feature 的 source/Pass 加入 domain；未来增加算法以 enum option 扩展，避免互斥 bool。`USE_LIGHTING` 是内置材质的静态选项，不能据 Shader 名字判断任意用户源是否受光照。Custom Toon 可以声明始终接受 Lighting/Shadows，也可以声明受自身某个静态选项控制。

Forward 选择规则：光照功能未启用时不接受 Shadows/Environment 维度；启用阴影 feature 且 Primitive receives_shadows、当前 View 阴影有效、构建 policy 支持 PCF 时选择 PCF，否则选择 Off。环境 feature 启用且场景有有效环境资源时选择 Sky，否则 Off。构建 policy 禁用 PCF 时，Renderer 本身也不得建立 PCF 的运行需求；资源损坏与 policy 禁用分别诊断，不能将前者默默当 Off。

CastShadows 仍是 CPU 场景过滤，与 Forward 接收阴影独立。receives_shadows 保留 Primitive 行为，用于选择程序；在完成迁移后删除仅重复选择结果的 `toy_receives_shadows` Object uniform，保持 canonical padding/codegen 并提升 ABI。cascade count、tile size、灯光数量、骨骼影响数继续为运行时数据，不额外编译变体。

Masked 与 two_sided 不混为一谈：Opaque/Masked 是标准表面源自己的静态 enum，控制 alpha clip；阈值是运行时参数。two_sided 沿现有材质状态继承改变 Cull，并由标准 PS 的面朝向处理背面法线/切线，不自动增加 bool shader dimension。Shader 若需要更复杂的双面算法，显式声明自己的静态维度。

### VS/PS 编译及程序查询

MaterialShaderMap 对应一个 Shader source revision、已解析的 Material 静态配置及目标 profile，保存不可变程序集合；内部查询条件为 role、VertexFactory 和该 Pass 的 typed permutation。每个结果是一个独立 ShaderMapProgram，包含已验证的 stages/reflection/layout，不再有 Local 主程序和 GPUSkin 附属程序。

MaterialInstance 可覆盖本地静态选项，engine selection 不出现在 Material 资产覆盖列表。Global 和默认 MeshPass 采用同一 permutation/entry/cache 机制，只是前者没有 VertexFactory、后二者没有用户 Material 静态域；共用集合查询实现，不另造三套 compiler。

静态 override 持久化为名字、明确 kind 与 bool/enum 值；ID 从规范化声明推导，不重复保存另一份权威值。实例支持继承和清除覆盖，未知名字、类型改变、删除的 enum 值均为错误；不开放 float 静态选项。

共享域不要求模板技巧：Core 保存 permutation domain/typed selection/records、VertexFactory 类型、声明式 Pass 协议、静态支持条件及格式定义，供 Tools 与 Runtime 共用；RenderCore 保存 MaterialShaderMap、immutable Program 与缓存；RenderScene 负责 View/Primitive 到 Pass selection 的动态选择与调度，不能作为 Tools 的依赖；compiler 完成源解析、组合规划、编译和 artifact 写入；Editor 完成源发现与请求/候选 UI。全部复用 Toy3dShaderFormat、Toy3dAssets、Toy3dAssetPipeline、Toy3dRuntime、Toy3dEditorCore，不新增库。

查询 key 与内容身份分开：

- Material 配置身份：Shader 逻辑身份 + typed static selection + target/profile；普通参数与 AssetId 不使相同配置重复编译。
- Program 查询身份：源/配置 + role + 可选 VertexFactory + Pass selection；源 revision 属已发布集合，不在 draw 中扫描目录猜最新文件。
- Stage compile key：实际 stage 源、entry point、受影响的静态/engine/VF 定义、依赖摘要、target/profile、ABI 与工具链。
- Program/entry content key：stage content、接口/layout、Pass state/template 等全内容；pipeline key 另含顶点布局、附件、有效 state，颜色或骨骼变化不创建新 pipeline。

v2 提供独立 VS/PS 源块，公共 include 的宏依赖纳入闭包；缺少明确 stage 影响范围的维度保守作用于 Both。只有 stage 编译输入确实无差异时才复用其编译结果。声明 PS-only 却在 shared include/VS 源中使用的维度必须报错或要求更正为 Both，不能隐式漏掉身份。改变插值接口的维度必须影响双方；链接验证 scalar shape、语义、location/interpolation 等接口条件。stage 字节码内容去重与编译请求去重分开，reflection 不用来反推未声明的 schema。

首批 stage 影响默认值：标准 Local/GPUSkin 选择仅影响 VS；Forward 的 SHADOW_MODE/ENVIRONMENT_MODE 仅影响 PS；PBR 的普通着色选项仅影响 Forward PS，SURFACE_MODE 与参与 alpha 的纹理/颜色选项同时影响各 coverage PS。标准 Forward VS 保持同源各变种的插值接口，法线/切线输出能力由源的标准接口声明决定；不得把实际改变 VS 输入/输出的优化偷偷记成 PS-only。ShadowDepth Opaque 无 PS，HitProxy 默认 PS 不因 factory 重编。

Material 保存 full 参数 schema；不同 Program 可有不同 active resources。Material/View/Pass/Object 绑定缓存按 active group layout + owner/value generation + resource view generation 查询，Material bind 与 Object bind 不再固定挂一份适用于所有 Pass 的对象。合法的无阴影程序无需 dummy shadow atlas；无法线贴图程序不要求 normal texture。相同 group layout 才共享 binding，所有 GPU refs 持续到真实 completion。

Material 保存不可变、Program-independent 的逻辑参数快照；RenderCore 按所选 Program 的 active layout 派生并缓存 binding，RHI 只处理 native mapping。默认纹理由 Shader 属性声明：内置白 BaseColor、白 MRO、平面法线及黑 emissive。active 的必需纹理为空且未声明默认时失败，inactive 资源不要求绑定；显式资源引用损坏仍失败，不静默替换为默认。

### 哪些组合需要编译

新增共享组合规划入口，CMake、Editor 与离线构建只负责提供输入，不能各自实现一套“默认加 GPUSkin”策略。输出是可验证的 required program 列表及其支持/排除原因，复用现有内容寻址 writer；Runtime/Shipping 的名单是此结果的不可变发布，不在 draw 现场规划。

规划输入包含发现的 Shader 源、源声明的合法域与条件、目标 profile、构建 feature policy、需要的 Material 静态配置、允许的 VertexFactories 和运行模式（Editor/Player）。项目源继续自动发现；policy 中的补充案例只是运行时选择需求，不变成手工源登记清单。

| 来源 | 编译选择 |
| --- | --- |
| 引擎启动必需 Shader | 当前功能要求的 Tonemap、UI、默认 MeshPass 及内置缺省材质配置；Editor 加 HitProxy，Player 排除 Editor-only role/source。 |
| 内置材质模板 | default 静态配置 + 当前资产实际使用/用户明确需要的其他配置；不把所有贴图功能无条件做全排列。 |
| 项目 Material/Instance | 解析已保存继承图，收集去重后的静态配置；未知/非法选择失败，不以 default 掩盖。 |
| 动态创建或脚本计划切换 | build policy 显式提供额外 typed 配置及 factory 范围，防止构建只看当前场景而漏掉运行需求。 |
| Editor 新草稿配置 | 按需求生成候选；提交前覆盖它可能使用的 engine permutations/factories，不能仅编译预览此刻的一种阴影状态。 |

对每个需要的静态配置，展开源声明与 policy 允许的 factory/Pass engine domain，再用 role/静态能力/profile 支持条件过滤。通常 lit Forward 是两种 factory × 两种 shadow × 两种 environment = 8 个查询组合；不使用 environment feature 的源最多 4 个，Unlit 不带这些 engine domains。Standard Opaque 的 ShadowDepth/HitProxy 引用共享默认集合，不按每个颜色、粗糙度或法线贴图重新生成深度程序。Masked 仅让 alpha/顶点相关选项进入这些 Pass 的有效输入。

policy 是有版本的 project/config 数据，使用现有 config/FileSystem；记录目标 profiles、允许的 engine feature 值、factory 范围和额外静态配置，内置默认由 engine/config 提供。schema 不接受任意 define 或原生 compiler 参数。缺省 Editor policy 包含 Local/GPUSkin、Off/PCF、Off/Sky；禁用某功能必须同时收紧 Runtime 可选择范围。

规划必须在展开前计算组合上界、在过滤后计算实际数量。首版每源最多 32 个 typed 维度、每 enum 最多 32 值；单个作业最多 4096 个 required Program 条目，单源最多 1024 个，超过限制明确失败并列出主要乘积维度，不能静默剥离合法需求。限制集中具名定义，与 reader/candidate 容量共同验证；分批编译不绕过最终集合完整性。输出 declared/required/filtered/cache-hit/stage 编译数量，便于定位组合膨胀，记录放构建结果而非长期文档。

集合索引已覆盖当前声明的 Pass/factory，见[现有接口](#完整集合索引与候选)；后续扩展为规划后的查询身份与 required coverage，加入 Pass selection records，并验证 source dependency 和 entry。Cook/独立部署使用同一索引，在现有 loose artifacts 上完成 required 组合发布；本轮不要求建立新的压缩 ShaderCodeLibrary。部署只包含 required 列表所引用的 entry/stage，不能删除 Shader 后从旧 Saved 目录复活。

### PBR 的具体算法

此次已直接核对本机 UE 5.5.3 的 `Engine/Shaders/Private/MobileGGX.ush`、`ShadingModels.ush::MobileSpecularGGXInner/GetEnvBRDF`、`BRDF.ush`、`ShadingCommon.ush`，以及 `MobileBasePassPixelShader.usf` 的 roughness 下界；不是 UE4.27 源码逐行验证。首版统一采用 Mobile 的 low-quality isotropic metallic/roughness 分支，即 `MobileSpecularGGXInner` 的 bHighQualityBRDF=false、GetEnvBRDF 的分析近似分支；不新增桌面/手机或高低 BRDF 质量维度。UE5.5 中的 Substrate、面积光、各向异性及附加多次散射能量补偿不在首版范围，不能宣称移植整个 Mobile renderer。公共参数见 [Epic 的 PBR 输入说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials?application_version=4.27)，简化思路见 [Epic Mobile PBR 文章](https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile)；该 2014 文章中的方向光 D_Approx 是历史近似，不与已核对版本的 D_GGX_Mobile 混用。

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

### PBR 必需资产与几何接入

当前 Texture2DAsset validation 固定 RGBA8 sRGB，runtime Texture 也只持 Texture2D；不能直接将法线/MRO/HDR 环境塞进现有路径。新增 texture import usage（Color、LinearData、Normal）并保存 import 设置：Color 在线性空间滤波后编码 sRGB，LinearData 按数据滤波，Normal 解码向量、滤波并归一化。runtime view 的色彩格式从已验证资产确定，不按绑定槽猜测或对同一 cooked asset 偷换格式。新资产必须显式记录 usage；旧格式拒绝并要求重新导入，不自动补成 Color。改变 usage 要重新导入，材质要求不兼容格式时提示具体资源。

usage 同时进入 cooked metadata 与 Shader sampled-property 要求；Color 使用 RGBA8 sRGB，LinearData/Normal 使用 RGBA8 UNorm。不能仅凭 UNorm 区分 Normal 与普通线性数据，也不根据变量名称猜要求。首版 normal map 固定 tangent-space XYZ 编码 [0,1]→[-1,1]、正 Y 约定；导入选项允许翻转绿色通道并记录，normal_scale 缩放 XY 后重新归一化。

增加环境光资产：仅包含版本化 GGX 预过滤 Cube payload，不设置球谐系数或预留对应占位数据。CPU 导入/烘焙留 Toy3dAssetPipeline，DTO/验证留 Core Assets。首版支持有界 Radiance HDR panorama，复用现有图像基础设施扩展 float decode，不顺带承诺 EXR。Cube face 方向、左手坐标、mip 粗糙度及数据布局是资产 contract；格式 R16G16B16A16Float、完整六 faces/mip、有限非负 HDR 值，Cube 大小不超过 512，源/解码/烘焙均检查预算。未知算法版本或 orientation 拒绝，不猜测匹配。

首版 mip 粗糙度为 mip/(mip_count−1)，采样 LOD 为求值 r × (mip_count−1)；mip 0 是未模糊的环境，后续用 N=V 的 GGX importance sampling 预过滤，积分权重按 NoL 归一化。Cube 顺序固定 +X/−X/+Y/−Y/+Z/−Z；以 s=2u−1、t=2v−1、纹理 v 向下，对应方向分别为 (1,−t,−s)、(−1,−t,s)、(s,1,t)、(s,−1,−t)、(s,−t,1)、(−s,−t,−1)，归一化后采样。panorama 的 u=atan2(x,z)/(2π)+0.5、v=acos(y)/π；导入与运行时不额外交换 Y/Z。

Runtime 为场景提供一个 owned environment snapshot（Cube 资源、旋转与强度），GT 通过现有 World/settings 与 SceneInterface 同 FIFO 发布，RT 负责资源；Scene 资产增加可选环境引用，新格式场景未配置环境时为 Off，旧格式要求按新结构重建。Preview 使用独立默认 studio environment 与预览灯光，不污染主场景；通过直接光照亮非金属表面，不暗中增加球谐或假环境漫反射。默认环境由离线工具生成并随引擎部署；引擎与项目环境共用资产加载。首版没有动态 probe 或 per-object environment，所以不为每个 Material 增加 Cube 属性及不同环境身份变体。

法线贴图增加 Tangent0：StaticMesh/SkeletalMesh 共享切线方向与 handedness，导入/构建按 UV seams/hard edges 生成或验证，缺有效 UV/tangent 的几何不能静默启用 normal map。当前依赖只有 Assimp 的 CalcTangentsProcess，未发现 MikkTSpace；首版统一采用 [MikkTSpace 官方实现](https://github.com/mmikk/MikkTSpace)，不能把自写近似命名为 MikkTSpace。作为 Tools 的固定 revision 构建依赖，源码缓存放根 build 下并支持离线指定已核对源，不改 engine/thirdparty，不让 Runtime 依赖生成器。构建时不跟随浮动 master，也不因缺依赖退回另一算法。旧 mesh payload 不由 Runtime 补切线或兼容读取，要求离线从源重建新格式，不能在 draw 热路径生成。

MikkTSpace 按 corner 生成结果，拆分顶点必须同步复制颜色、所有 UV、骨骼索引/权重及其他顶点属性并重建 indices。资产显式记录有效切线状态；NormalMap=Off 可使用已初始化的安全切线，启用时必须具备有效 UV/切线。环境 Cube 大小限定 2..512 的二次幂并包含完整 mip 链；源/烘焙结果不能表示为有限非负 FP16 时导入失败，不静默截断。材质的数值范围由 schema 与编辑/加载边界共同验证。

蒙皮位置/切线按线性变换 rows，法线按已有 inverse-transpose normal rows；随后 Gram-Schmidt 正交化切线并用 handedness 重建 bitangent。Object 非均匀 scale 同样处理；退化 frame 报告并使用具名安全法线，不能产生 NaN。重用现有六 Float4/bone typed 数据，不新增骨骼 StructuredBuffer，不改变正 scale 与 4/8 共用 Shader contract。切线和新的法线约定要覆盖 static、GPUSkin、镜像 UV 和三种 Pass。

PBR 不在材质 Shader 内做 tonemap/gamma；仍输出线性 HDR，经当前 Tonemap/UI 路径。新增 HDR 纹理/Cube 时逐 profile 检查 sampled/filter/limits support；Vulkan ES3.1 不因桌面能力抬高 SPIR-V 版本、descriptor set 数或可选能力要求。能力不足明确不支持该环境配置，不能上层判断 Vk/D3D 后偷改算法。

### 生命周期、错误与 Editor

Source/domain、CPU 资产、ShaderMap revision 不可变并可共享；MaterialInterface/Library/编辑草稿是 GT owner，MaterialRenderProxy 与 binding/cache 是逻辑 RT owner。组合规划/编译可走现有 Editor worker；只把 owned 候选发送 GT，再 FIFO 发送 RT 验证结果，不从 worker 读可变 World/Proxy/RHI。不新增线程池、Process 封装或全局材质服务。

改变静态选项、父材质、源码或构建 policy 需要完整候选。先校验 source/domain/full schema/required coverage，再校验每种所需 stage/link/VertexFactory/附件及 binding，成功后一次提交完整配置图；失败保留旧参数、静态配置、Proxy 地址、程序集合与画面。普通参数仍无需编译。源重命名/删除、新旧 domain 不兼容和孤儿静态 override 具体报告；不能猜 default 将错误候选当成功。多个实例解析同一配置复用 map，编辑草稿与已保存 Library 不共享可变状态。

factory 编译支持与实际几何属性分开验证。材质赋值时校验目标 mesh；更新共享材质时校验所有当前使用者，任何对象/slot 缺少新要求的属性或 role 都拒绝整个候选发布，并保留旧有效配置。诊断列出对象、slot 与缺失项，草稿仍可编辑；不部分发布或让不兼容对象消失。

Editor UI 区分 Properties、Static Options 与只读 Shader capabilities；只显示该 Shader 声明的选项。编译请求标明目标配置与所需组合，编译完成之前继续显示旧效果；提供来源/Pass/VF/selection/profile 的缺失诊断。不在 draw 执行文件发现、加载、编译或 schema 修复；启动/加载预检 mandatory 集合。运行时选择缺失明确失败，不近似选最近 variant，也不自动用 Local 替代 GPUSkin。

资源更新与发布仍遵守 recording/discard/submit 与 GPU completion；候选预检不额外 wait_idle。环境/Cube 或线性 texture 的失败资源不会暴露半初始化状态；旧 binding/program/resource refs 持至提交完成。Cook/Player 启动缺失 required entry 必须失败并给出身份，不因 Editor 曾经成功编译过而跳过 validation。

### 后续使用方式

以下为新方案完成后的操作流程，不代表当前已可调用的 API：

1. 普通材质：选择 Unlit/Phong/PBR 或项目 Shader，创建 Material，设置颜色/标量/贴图及本地静态选项；创建 MaterialInstance 时可覆盖这两类值，赋给 StaticMesh/SkeletalMesh 的材质槽。纹理导入时选择 Color/LinearData/Normal，PBR 的 MRO 固定 R=AO、G=Roughness、B=Metallic。
2. 场景控制：Primitive 设置投影/接收阴影，场景设置镜面环境资源、强度与旋转；用户不手选 Local/GPUSkin 或 Off/PCF/Sky 程序。光照开关仅显示在声明它的 Shader 上，two_sided 改 Cull 与标准背面处理，不额外生成变体。
3. 自定义 Shader：在 project/shader 创建新语言源，声明 Properties、Variants、roles、factory/feature 支持及 stage 影响；Standard 编写着色/coverage 函数并使用标准入口，Custom 自写 VS/PS 及所需阴影/拾取 Pass。Engine 的 Global/default MeshPass 不出现在创建材质列表。
4. 编译与发布：Editor 修改静态选项后生成并预检完整候选，成功后一次发布；普通参数变化只更新数据。发行构建从资产与 policy 收集配置，额外动态配置要显式列入 policy；运行时只切换已部署配置，缺失时报错且保留当前有效状态。
5. 渲染查询：从 MaterialRenderProxy 的已发布 MaterialShaderMap，按 role + 实际 VertexFactory + Pass selection 取得 Program，再按 active layout 准备 bindings/pipeline。标准 Opaque 的默认深度/拾取结果可引用引擎共享集合；Masked/Custom 按已验证源的 role 取得程序，不扫描缓存或派生 skin key。

例如，同一 PBR 材质赋给静态网格和骨骼网格时复用相同 Material 配置，分别查询 Local/GPUSkin；开关接收阴影只改变 Forward 的 Pass selection，颜色/roughness 变化只改参数，USE_NORMAL_MAP 变化需要另一个静态配置。所需引擎组合均预先编译，用户不管理它们的宏。

### 验证与旧入口删除条件

先迁移不改变生产格式的共享 domain 基础，随后以一个可构建的纵向批次同时切换 v2 EBNF/AST/parser/codegen、产物 reader、内置源/模板/测试和部署；集合加载/Material 静态继承、mesh Pass 与绑定、PBR/资产基础、Editor/部署继续分批推进。不能为了保持可构建加入旧格式兼容 reader。集合索引在全部所需 entry 验证成功后最后发布；全部按本节同一方案，不在每批新增另一份总体设计，不缩减最终验收范围。

必须覆盖声明/default/排序与稳定身份、重复/非法/条件过滤/组合预算、Local/GPUSkin 同等查询、stage-only 复用与跨 stage 接口错误、source revision 与索引篡改、静态实例继承/保存/循环/孤儿/回滚、缺失 mandatory 程序、无阴影/无 normal map 的资源缺省、active-layout 缓存失效、透明裁剪三 Pass 一致、Player 排除 Editor-only 及额外运行时配置部署，以及旧语言/产物/受影响资产版本明确拒绝、不自动迁移。

PBR 独立 CPU reference 与 GPU readback 验 F0、metallic 端点、Mobile GGX 及 2048 上界、简化 visibility/EnvBRDFApprox、roughness 下界、grazing、零灯/自发光、禁用光照、带/不带 shadow/镜面 IBL、不同参数不重编；测试不是复制同一 helper 自证。增加 constant-white 环境/方向 face 图检查预过滤、Cube orientation、粗糙度 mip，验证无直接光时不产生环境漫反射，并验证 Color/LinearData/Normal mips 与 texture usage 保存/重新导入。真实 Vulkan 场景覆盖 StaticMesh/GPUSkin 4/8、阴影、拾取、Masked、非均匀 scale、预览/缩略图、旧资源 GPU 保活和候选失败。手机、D3D 支持必须有实际 profile/backend 证据，未运行不能声称通过。

新 compiler 只接受 Shader language v2，v1 明确报版本不支持；内置、模板、项目示例与测试同批重写，不提供兼容 parser 或旧 Pass 隐式映射。entry format 已提升到版本 7，compile-request 已提升到版本 2；Material permutation record 保持现有规范化格式，factory 迁移为独立身份字段，旧 compiled artifacts 拒绝并要求重编译。后续 Pass selection/ABI 按实际 contract 变更提升对应版本。Material/Instance、texture import、mesh payload 与 Scene/environment 同样按实际变更提升格式版本，reader 只接受新 contract；需要更新的仓库资产显式重建或重新导入，不保留自动转换链，不覆盖用户原文件。

已删除独立的 mesh permutation 入口、旧 factory 类型名、派生 key 与程序目录扫描，并改为显式声明支持范围及完整集合索引。`gpu_skin_program` 数据/访问器、`MeshBatch::resolve_program` skin 分支及材质单 Program 发布路径已删除；Editor 的单 Forward/Local 限制已解除，仍需完成 Standard/Masked 包装、Pass selection/planner、stage 编译复用和 PBR 接入；保留真实 ShaderMap/entry/cache/resource 共享基础设施，不保留转发头、旧 target alias 或永久双轨。完成后将本节拆回正文及各主文档职责处，只保留一个方案入口和真实接口。

### 参考依据

- [Unity URP Pass tags](https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/manual/urp-shaders/urp-shaderlab-pass-tags.html)：借用 role 协议与自定义 Shader 接入思路，不借用 URP 标签文本作为 Toy3d API。
- [Unity 官方 URP Lit 源码](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/Lit.shader)：内置材质通过同类 Pass、Material keywords 与 pipeline keywords 组合的依据，不照搬 Unity 全局 keyword 状态。
- [UE4.27 Shader Development](https://dev.epicgames.com/documentation/en-us/unreal-engine/shader-development?application_version=4.27)：Material/VertexFactory/ShaderType 组合、支持条件过滤与默认深度材质复用的职责依据。Toy3d 仍以 role 对应的完整 Program 为查询单位。
- UE 5.5.3 源码核对入口：`MobileGGX.ush::D_GGX_Mobile`、`ShadingModels.ush::MobileSpecularGGXInner/GetEnvBRDF` 的 low-quality/分析近似分支、`BRDF.ush::EnvBRDFApprox/EnvBRDFApproxLazarov`、`MobileBasePassPixelShader.usf` 的 roughness 下界、`ShadingCommon.ush::DielectricSpecularToF0/ComputeF0`、`DeferredShadingCommon.ush` 的 DiffuseColor 计算，及 `MaterialShared.h::FMeshMaterialShaderMap/GetMeshShaderMap/GetShader`。版本与历史 Mobile 文章分别记录，不能把不同年代的直接光算法混成同一个实现。
