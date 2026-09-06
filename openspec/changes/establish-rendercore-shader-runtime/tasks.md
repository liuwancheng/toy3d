## 1. RenderCore graphics state adapter

- [x] 1.1 新增 `rendercore/shader/shader_graphics_state.h/.cpp`，实现 checked `ShaderGraphicsPassState`→`RHIGraphicsPipelineDesc` candidate 转换，保证只写 Shader-owned 字段且失败不修改基础 descriptor；以 `Toy3dShaderGraphicsStateTests` 覆盖全部合法枚举、active attachment blend/write mask、reversed-Z state 与非法枚举诊断。
- [x] 1.2 将 `ForwardSceneRenderer` 的 pipeline descriptor 组装改为调用新 adapter，并在同一提交删除匿名 namespace 中全部 `to_rhi_*`/本地 apply 转换和默认回退；构建并运行 Forward/Shader graphics state 相关测试，且 `rg "to_rhi_(primitive|cull|front|polygon|compare|stencil|blend|color)" engine/runtime/renderscene` 无残留。

## 2. Device-scoped RHI Shader Program cache

- [x] 2.1 在唯一所属 spec 已登记类型的前提下新增 `RHIShaderProgramKey` 完整值语义及 hash/equality，覆盖Program identity、permutation、mapping version、stage/entry/content hash和logical/target binding layout，排除debug name与对象地址；用定向单测验证逐字段差异产生miss且强制容器hash collision不错误复用。
- [x] 2.2 新增 logical-RT-only `RHIShaderProgramCache`，以 non-owning `RHIDevice&` 创建完整 candidate、成功后发布共享不可变 `RHIShaderProgram`、失败不做negative cache；用fake device测试hit只创建一次、每个创建阶段fault injection、原始RHI code、partial ref释放和后续重试。
- [x] 2.3 在Renderer device初始化后创建cache，并把Forward Base Pass的逐帧`create_rhi_shader_program()`改为cache查询；用连续两帧/多个draw的测试证明同一完整Program identity只触发一次stage/layout创建且不同Program不混用。
- [x] 2.4 迁移全部调用方后删除公开直通`create_rhi_shader_program()`，只保留descriptor构建和cache正式入口；以`rg "create_rhi_shader_program" engine/runtime --glob '!rendercore/shader/rhi_shader_program_cache.cpp'`无结果和相关target构建通过作为验证。

## 3. Global Shader CPU domain

- [x] 3.1 新增`GlobalShaderType`与`GlobalShaderBindingRequirement`不可变值契约，实现稳定type name、shader/pass/permutation、exact stage及logical binding requirements；单测覆盖descriptor equality、重复type name和不含ShaderPlatform/native slot/RHI ownership。
- [x] 3.2 新增`GlobalShaderMap`与`GlobalShaderMapResult`，只通过现有`ShaderMap::find_or_load()`构建全有或全无candidate，并按统一ShaderPlatform验证stage/binding schema后冻结typed lookup；单测覆盖cache reuse、缺失type、platform mismatch、stage/binding mismatch、partial candidate不发布和冻结后只读。
- [x] 3.3 在Tonemap与ImGui各自feature模块旁声明返回`const GlobalShaderType&`的普通函数，并新增无状态`renderer_builtin_shaders.*`按`enable_imgui`组装required type指针集合；测试验证Tonemap始终包含、ImGui禁用时不包含且不触发I/O、启用时加载失败使整组失败，并确认代码中不存在GlobalShader registry/注册宏/可变全局单例。

## 4. Engine composition root 与平台选择

- [x] 4.1 将runtime `ShaderPlatform::VulkanPortableV1`与离线`ShaderCompileProfile::VulkanPortableV1`全链路重命名为`VulkanES31`，同步错误文本、测试、AGENTS.md、Active设计文档和main specs；保持枚举持久化数值及既有ShaderMapEntry读取结果不变，不新增旧名alias，并以reader golden test及`rg "VulkanPortableV1|VulkanPortable v1" AGENTS.md document engine openspec/specs project`无结果验证（不改`document/archive/`及既有OpenSpec change/archive历史产物）。
- [x] 4.2 在现有Engine composition root集中实现runtime backend configuration→`ShaderPlatform`映射，移除内置Shader路径中的`VulkanES31`硬编码；单测或配置测试覆盖VulkanES31、D3D11SM5、D3D12SM6选择及未构建/无产物backend的可诊断失败。
- [x] 4.3 将Engine内置Shader初始化改为GT创建loader/`ShaderMap`、选择required types并构建冻结`GlobalShaderMap`，在任何RenderingThread启动前处理CPU加载失败；更新初始化回滚测试，验证失败不启动Renderer且保留原始Shader诊断。
- [x] 4.4 删除Engine的逐项`tonemap_shader_program`/`imgui_shader_program`字段与硬编码`ShaderMapProgramKey`，改为持有单个冻结map并按Renderer teardown、RenderingThread join、GlobalShaderMap、ShaderMap、loader顺序释放；用ownership/析构顺序测试和`rg`检查确认旧字段、字符串加载入口无残留。

## 5. Global Shader Pass 与 Renderer bootstrap

- [x] 5.1 一次性把Renderer构造输入从逐项Program改为`std::shared_ptr<const GlobalShaderMap>`，不保留兼容重载；更新所有生产/测试调用点并通过编译验证新增内置Pass不会扩张构造签名。
- [x] 5.2 将Tonemap初始化改为按`GlobalShaderType`查询CPU Program并经Renderer-owned cache获取RHI Program，删除shader/pass/platform重复校验和直接RHI创建；更新`Toy3dTonemapPassTests`验证缺失type、RHI创建失败、正确binding及成功路径。
- [x] 5.3 将启用的ImGui初始化迁移到相同GlobalShaderMap/cache路径，保留font atlas bootstrap ownership但删除identity/platform与直接RHI创建；更新ImGui/Renderer测试验证disabled不查询、enabled缺失失败、font upload与Program candidate全有或全无。
- [x] 5.4 重排Renderer logical RT bootstrap为device→program cache→manager/scene基础对象→required Program/Pass candidates→placeholder/font submission及exact completion→viewport→Running，并更新single/multi-thread、各步骤fault injection和first-error测试，证明任何失败不发布部分Pass或普通façade。

## 6. Teardown、边界清理与一致性检查

- [x] 6.1 落实正常与terminal RT teardown顺序：Scene/non-owning collections→representations→Pass/Program refs→program cache→placeholder→有界queue cleanup→viewport/device→Renderer map ref；用析构探针测试所有RHI refs早于device且Engine最终map owner晚于RenderingThread join。
- [x] 6.2 为DeviceLost和状态未知路径增加测试，验证不无限wait、不继续不安全native调用、cache CPU entries可有限释放且secondary cleanup diagnostic不覆盖Renderer first error。
- [x] 6.3 检查RenderCore/RenderScene/RHI依赖边界：公共RHI不包含`ShaderPlatform`/Shader format，RenderScene无Vk/D3D类型和backend判断，Pass无Shader文件I/O或直接shader/layout创建；用`rg`审计与全部相关单测通过作为验证，并修正发现的直接违规而不扩张到无关目录重构。
- [x] 6.4 更新`engine/runtime/CMakeLists.txt`显式登记三个新增测试target，遵守C++17、UTF-8和target-scoped设置；从现有build tree重新configure并构建这些target，确认CTest均已登记。

## 7. 独立构建与运行验证

- [x] 7.1 主实现者完成代码复查，逐项核对五份delta specs、Type Contracts、C++17特性注释、无兼容双轨和所有旧入口删除；运行OpenSpec strict validate并记录通过结果。
- [x] 7.2 由独立sub-agent使用`verify-toy3d-build`重新执行Windows x64 Debug CMake configure，构建`Toy3dEditor`、三个新增测试target及所有受影响Renderer/Shader/RHI测试target，再运行`ctest --test-dir build -C Debug --output-on-failure`；主实现者根据独立证据修复后复验至通过。
- [x] 7.3 由独立验证者运行启用Vulkan backend的Toy3dEditor真实draw/present/正常关闭冒烟，核对Tonemap与启用ImGui路径无validation error且日志正常刷新；明确记录D3D11、D3D12、移动端及未运行配置为未覆盖，不以Vulkan结果替代三后端验证。
