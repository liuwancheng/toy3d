# Shader：语言、ABI、编译与运行时

## 定位与当前范围

tools/shader_compiler 为 frontend/layout/codegen/CLI，rendercore/shader 为 ShaderMap/Loader/Program/typed parameters，engine/shader 与 project/shader 是源码。Toy3dShaderFormat 是独立格式库，源码位于 engine/core/shader，依赖 Core，不依赖 Runtime 或 compiler；engine/shader 只保存 shader 源码和构建规则。

Shader logical schema 是权威；reflection 验证字节码并提供 native mapping，不能反向补出未声明业务参数。RHI 不解析 .shader、C++ generated metadata 或 Material 属性。

当前生产外部编译入口 compile-vulkan；D3D11 FXC/SM5、D3D12 DXC 的工具链设计不等于已完成外部编译或后端。Cook/Shipping ShaderCodeLibrary 的只读去重发布仍为演进范围，不能虚构完整 cooker/loader。Editor 消费已验证 loose ShaderMapEntry。

## 语言与源码边界

语言 Version 1：不改变既有语义的可选增量可在 v1 扩展，旧 compiler 明确 UnsupportedLanguageFeature；packing/默认语义变更提升 language/ABI major，未知关键词错误。

语法权威 [shader-language-v1.ebnf](shader-language-v1.ebnf)，可用真实示例 engine/editor/tests/fixtures/shader/painted.shader 与 engine/shader 内置文件，不写想象的 DSL。

- Properties 是 Material 属性，保留源码顺序/typed default/UI；不是任意 HLSL struct。v1 不开放属性数组，矩阵为 Matrix4x4。
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

schema hash（含 default/UI/sampler preset）、logical layout hash、target binding hash、bytecode hash 分离；compile/package/dependency 内容寻址 SHA-256，parameter identity 保持 FNV。

## Variant/permutation

VariantId/EnumValueId 用带 little-endian uint32 长度前缀的 FNV domain；variant name 与 enum option 的稳定身份不随声明排序变更。0 invalid，rename 新身份，kind 改变进入 permutation record。

compiler 按 schema 补 defaults，拒绝未知/重复/非法选择和碰撞；按 VariantId 排序，序列化版本/count/ID/kind 与 bool uint32 0/1 或 EnumValueId，SHA-256 得 permutation key；空 domain 也有非零带版本 key。

generated prelude 的 TOY3D_VARIANT_* 名字检查碰撞；enum options 按 ID 生成 dense integer，不依赖源码顺序。不能用任意 HLSL define 绕 typed selection/key。

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

## ShaderMapEntry v2 与缓存

目录按调用方期望 shader_map_key 定位，reader 重算身份，不相信 manifest 自报 key/扫描猜测。required 文件：manifest.txt、mapping.txt，每 stage 的 manifest/spv/reflection/dependencies；vertex、vertex+pixel、compute 组合需 stage/entry/mapping/reflection 一致。

- UTF-8 LF、唯一 key=value、版本定界 record；未知/重复/缺字段、非 canonical 数字/小写摘要、非法 flags/enums/paths、重复 binding/stage、未排序 dependencies 拒绝。
- manifest ≤64 KiB，metadata 单文件 ≤4 MiB，stage binary ≤64 MiB，各类 record ≤4096。先限大小/hash，再解析/重算 semantic hash，最后重算 shader_map_key/entry_content_hash。
- shader_map_key 覆盖 Shader/Pass、target/profile、mapping/layout、template、Variant/permutation 与 stage compile/reflection/binary；entry_content_hash 另含具名 mapping/full dependency record，区分同 key 内容冲突。
- writer 用 owned staging/no-replace rename；已存在必须用同 reader 全验证且 key/content 相同才 cache_hit，损坏/冲突诊断、不覆盖/删 final。
- reader 只验 artifact，当前源码/include/toolchain identity 由 compile/cache 调用方验证；不存在“缓存文件在就可信”的捷径。

## Editor 全量重编译

内置登记统一由 engine/shader 的构建描述生成，记录逻辑名、源码、Pass、用途与部署子目录；项目材质源由 parser 自动发现。全量动作编译全部内置登记/项目发现源的当前 Vulkan ES3.1/default permutation，声明身份由 parser 校验，不枚举尚未支持的全部 permutation。默认 permutation 必须由 compiler 的 typed domain/default selection 解析；例如 Unlit 的 USE_VERTEX_COLOR=false，不能用空 domain key 或首个缓存项代替。Include 作为依赖验证并参与构建失效。

Editor 独占编译队列一次一个任务，复用 Core Process/Thread；失败继续下一项，取消保留已提交版本。材质逐项预检和发布；Tonemap/ImGui/HitProxy 完整候选作为一组预检后在 RT 帧边界接管，ShadowDepth 独立验证。管线采用候选 Shader 的 Pass state；ImGui/Tonemap 没有 depth attachment，启用 depth/stencil 必须拒绝。C++ generated ABI 不兼容时要求重建程序；旧 GPU refs 按原提交生命周期保活。

Saved 恢复先验证源码/include/产物；无效时验证部署版本，有有效回退才降为 Warning。Program 可用性与最近编译结果分开；真实编译/读取/验证错误保留具体诊断，不能把所有失败归为源码变化。Tools → Shaders 提供重编译/取消；右下角消息卡片展示结构化阶段、进度、结果，所有诊断同时进入 Console 与文件。

Tools → Create → Shader 创建项目 Material Shader，模板来自现有 Unlit/Phong 源码，名称限定 Project/Surface/，路径限定 project/shader 内规范相对 .shader；不开放 Global/生成 ABI 创建。创建先解析模板与新声明、拒绝重复/越界/已存在路径，以 Core CreateNew 原子发布单文件源码，再刷新发现索引；外部冲突保留源文件并诊断，不新增人工登记清单。源码创建、编译与 Material 创建分别反馈，不因编译失败删除源码。验证在隔离目录覆盖真实 Unlit/Phong 编译、冲突/非法路径/不覆盖和重启发现。

## 项目源码自动发现

工程根由 [Runtime](runtime.md#工程与分层配置) 注入。启动、创建成功和 Recompile 前调用 compiler/shader_source_discovery.h 的 discover_shader_sources(files, root)，通过 Core FileSystem 有界扫描 shader/**/*.shader、复用 Parser 提取名称和 Pass 名称列表、诊断重名；.hlsli 只参与 include 依赖。发现器不限制名称前缀或 Pass 用途，项目材质的 Project/Surface/ 和单 Forward Pass 规则由 Editor ShaderWorkflow::read_sources 校验。没有文件监听，批次固定快照；无工程仅处理内置源。

项目名称限定 Project/Surface/，当前支持一个 Forward Material Pass，不猜第一个 Pass。最多 250 项、2048 目录、16 层、单源 4 MiB、总源 64 MiB；确定顺序，拒绝链接。重名的所有项、损坏/不支持的声明带路径诊断并计入批次失败，合法项继续编译。损坏的外部编辑保留已发布的旧 Program，并可定位 parser 行号；重名/删除移除查询身份。刷新按逻辑身份保留旧 Program，后续编译/恢复仍验证 source/include/hash；索引身份不代表当前源码已验证。删除/冲突项不从 Saved 复活，已有 GPU refs 保持既有提交生命周期；改声明名使旧名称引用失效，移文件不改变逻辑名。

Create Shader 是验证后单文件 CreateNew，再刷新发现索引；外部冲突明确报错并保留文件，不增加清单/两文件回滚。shader_sources.txt 已移除；Saved/编译产物的 manifest/hash 校验仍保留，builtin_shader_sources.h.in 仍表达内置用途/ABI/部署记录。测试夹具位于 editor/tests/fixtures/shader，不要求项目保留示例 Shader。发现失败路径见 editor/tests/project_tests.cpp；真实创建/编译/重启发现见 material_shader_tests.cpp。项目 Build Game/Cook/编译库部署尚未接入，不能声称 runtime 会现场编译项目源码。

## 修改与验证

改语言/ABI 同批修改 parser/AST/layout/generated C++/HLSL/reflection/manifest reader/真实示例；确认版本和内容身份变化。Vulkan 验 explicit offset、reflection 与 spirv-val --target-env vulkan1.1；后端 slot 不成为通用语义。

测试 tools/shader_compiler/tests/frontend_tests.cpp、layout_tests.cpp、compile_tests.cpp；runtime/tests/shader_parameters_tests.cpp、generated_shader_parameters_compile_tests.cpp、shader_map_tests.cpp、shader_map_entry_loader_tests.cpp、global_shader_map_tests.cpp、shader_graphics_state_tests.cpp。构建 target 从 CMake 查，失败/非法布局/缓存篡改和跨层验证比复制 happy-path 更重要。
