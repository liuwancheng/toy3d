# Shader：语言、ABI、编译与运行时

## 定位与当前范围

tools/shader_compiler 为 frontend/layout/codegen/CLI，rendercore/shader 为 ShaderMap/Loader/Program/typed parameters，engine/shader 与 project/shader 是源码。Toy3dShaderFormat 是独立格式库，源码位于 engine/core/shader，依赖 Core，不依赖 Runtime 或 compiler；engine/shader 只保存 shader 源码和构建规则。

Shader logical schema 是权威；reflection 验证字节码并提供 native mapping，不能反向补出未声明业务参数。RHI 不解析 .shader、C++ generated metadata 或 Material 属性。

当前生产外部编译入口 compile-vulkan；D3D11 FXC/SM5、D3D12 DXC 的工具链设计不等于已完成外部编译或后端。Cook/Shipping ShaderCodeLibrary 的只读去重发布仍为演进范围，不能虚构完整 cooker/loader。Editor 消费已验证 loose ShaderMapEntry。

## 语言与源码边界

语言 Version 1：不改变既有语义的可选增量可在 v1 扩展，旧 compiler 明确 UnsupportedLanguageFeature；packing/默认语义变更提升 language/ABI major，未知关键词错误。

语法权威 [shader-language-v1.ebnf](shader-language-v1.ebnf)，可用真实示例 project/shader/painted.shader 与 engine/shader 内置文件，不写想象的 DSL。

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

## 修改与验证

改语言/ABI 同批修改 parser/AST/layout/generated C++/HLSL/reflection/manifest reader/真实示例；确认版本和内容身份变化。Vulkan 验 explicit offset、reflection 与 spirv-val --target-env vulkan1.1；后端 slot 不成为通用语义。

测试 tools/shader_compiler/tests/frontend_tests.cpp、layout_tests.cpp、compile_tests.cpp；runtime/tests/shader_parameters_tests.cpp、generated_shader_parameters_compile_tests.cpp、shader_map_tests.cpp、shader_map_entry_loader_tests.cpp、global_shader_map_tests.cpp、shader_graphics_state_tests.cpp。构建 target 从 CMake 查，失败/非法布局/缓存篡改和跨层验证比复制 happy-path 更重要。
