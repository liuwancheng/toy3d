## Purpose

在通用进程级 `ShaderMap` 之上定义内置引擎 Shader Program 的显式类型、加载集合和只读查询边界，使 Tonemap、ImGui 及后续内置 Pass 不再散落字符串 identity、平台判断和重复校验。

## ADDED Requirements

### Requirement: Global Shader 使用显式类型而非可变 registry
每个内置 Shader Program SHALL 由一个显式 `GlobalShaderType` 值描述稳定 shader/pass identity、permutation、Program kind、required stages 与 binding schema。Toy3d 的一个 `GlobalShaderType` MUST 对应当前一个多 stage `ShaderMapProgram`，不同于 UE4.27 中通常对应单 stage shader 的类型。

Global Shader 类型集合 MUST 由明确可搜索的代码声明与启动加载计划组成；不得引入静态初始化注册宏、可变进程级 registry、多态 shader type 基类、service locator 或新的全局单例。类型声明不得拥有 loader、`ShaderMap`、`RHIDevice` 或 RHI refs。

#### Scenario: 声明 Tonemap
- **WHEN** RenderCore 声明内置 Tonemap Global Shader
- **THEN** 单个 `GlobalShaderType` MUST 标识其完整 vertex+pixel Program 及预期 binding schema，而不是为两个 stage 建立两个运行时类型

#### Scenario: 新增内置 Pass
- **WHEN** 后续新增 Copy Global Shader
- **THEN** 开发者 MUST 增加显式 type 声明与 composition-root load request，不得增加 Renderer 构造参数或依赖静态注册副作用

### Requirement: GlobalShaderMap 组合使用 ShaderMap
`GlobalShaderMap` SHALL 通过现有 `ShaderMap::find_or_load()` 完成 CPU Program 加载、格式验证和进程级 cache reuse，再把成功 Program 按 `GlobalShaderType` 建立不可变领域索引。它 MUST NOT 重复文件 I/O、ShaderMapEntry parsing、Program validation、parameter index 或字节码存储。

构建成功后 `GlobalShaderMap` MUST 冻结；RT 查询只返回不可变 `ShaderMapProgram` 强引用。未知 type、未请求 type 或 identity/schema 不匹配 MUST 返回可诊断失败，不得返回空对象后让具体 Pass 猜测原因。

#### Scenario: ShaderMap 已缓存 Program
- **WHEN** GlobalShaderMap 请求一个已存在于进程级 ShaderMap 的等价 Program
- **THEN** 它 MUST 复用同一个不可变 CPU Program，不得重新读取 Shader 文件

#### Scenario: Type schema 不匹配
- **WHEN** 加载结果缺少 GlobalShaderType 声明的 pixel stage 或 required binding
- **THEN** GlobalShaderMap 构建 MUST 失败并报告 type、shader/pass identity 与不匹配字段

### Requirement: Required 与 Optional 由启动加载集合表达
Required/Optional MUST 表达某次 composition-root 启动选择，而不是名为 pass eligibility 的长期对象、枚举或状态。composition root MUST只把本次启用的 Global Shader types 放入 required load set；set 中任一加载或 type validation 失败 MUST使整个 `GlobalShaderMap` 构建失败且不发布部分 map。Optional 功能未启用时 MUST不把对应 type 放入 set，也不得发起其 I/O；一旦启用并加入 set，失败 MUST同样阻止 Renderer 启动。

Tonemap 在当前 Renderer 中 MUST 始终为 Required。ImGui 在 `Renderer.EnableImGui=false` 时 MUST 不请求；启用时 MUST 作为 Required 加载，不能在失败后静默隐藏 UI。

#### Scenario: ImGui 禁用
- **WHEN** composition root 选择不启用 ImGui
- **THEN** load plan MUST 不包含 ImGui type，GlobalShaderMap 构建不得因其产物缺失失败

#### Scenario: ImGui 启用但产物缺失
- **WHEN** composition root 启用 ImGui 且 ImGui Program 加载失败
- **THEN** GlobalShaderMap 构建与 Engine 初始化 MUST 失败并保留 ShaderMap 诊断

### Requirement: ShaderPlatform 只在 composition root 选择
GT composition root MUST 根据所选 RHI backend/configuration 选择一个与 Cook/runtime contract 一致的 `ShaderPlatform`，并用该 platform 构造全部本次 Global Shader keys。Vulkan 的 runtime platform 与离线 compile profile MUST 分别统一命名为 `ShaderPlatform::VulkanES31` 与 `ShaderCompileProfile::VulkanES31`，文档术语 MUST 使用 `Vulkan ES3.1 profile`；该名称对应的能力 contract 仍为 Vulkan 1.1、SPIR-V 1.3 与最多四个 bound descriptor sets。持久化 profile 数值 MUST 保持不变，源码和公共头文件 MUST NOT 保留 `VulkanPortableV1` compatibility alias。

`GlobalShaderMap` MUST 验证所有 entries 属于同一所选 platform；具体 Tonemap、ImGui、SceneRenderer 和 RenderScene pass MUST NOT 写死 `ShaderPlatform::VulkanES31`、检查 backend 名称或自行重写 Program key。

公共 RHI MUST NOT 包含或依赖 `ShaderPlatform`。未配置 backend→platform 映射、产物 profile 不匹配或所需 capability 不支持时，初始化 MUST 返回可诊断失败。

#### Scenario: 选择 D3D12 backend
- **WHEN** runtime configuration 选择 D3D12
- **THEN** composition root MUST 请求 D3D12SM6 Global Shader Programs，Pass 代码保持不变

#### Scenario: Map 混入其他 platform
- **WHEN** 一个 Vulkan Program 被放入 D3D11 GlobalShaderMap candidate
- **THEN** candidate MUST 被拒绝且不得冻结发布

#### Scenario: Vulkan profile 重命名
- **WHEN** runtime、Shader compiler或ShaderMapEntry reader表示现有Vulkan移动公共基线
- **THEN** 源码 MUST只使用`VulkanES31`名称，序列化与读取既有产物的数值identity MUST保持不变，且旧名称不得继续作为alias可编译

### Requirement: Pass 只按 GlobalShaderType 查询
Tonemap 与启用的 ImGui 初始化 SHALL 从冻结 `GlobalShaderMap` 按其显式 `GlobalShaderType` 查询 CPU Program，再通过 device-scoped `RHIShaderProgramCache` 获取 RHI Program。具体 Pass MUST NOT 硬编码 shader name/pass name、重复验证 platform、直接调用 `RHIDevice::create_shader()`/`create_binding_layout()`，或拥有进程级 loader/cache。

查询成功只证明 CPU Program 满足 type contract；RHI object 创建成功才允许发布对应 Pass resources。查询或 RHI 创建失败必须沿 Renderer bootstrap first-error path 返回。

#### Scenario: Tonemap 初始化
- **WHEN** Renderer 在 logical RT 初始化 Tonemap resources
- **THEN** Tonemap MUST 使用其 GlobalShaderType 查询和共享 Program cache，且不感知 Program 来自 Entry loader 还是未来 Code Library loader

#### Scenario: Pass 请求未加载 type
- **WHEN** Pass 请求不在冻结 map 中的 GlobalShaderType
- **THEN** 初始化 MUST 可诊断失败，不得在 Pass 内临时回退到字符串加载

## Type Contracts

| 类型 | UE4.27 术语参考与 Toy3d 职责 | 所有权与生命周期 | 可变线程 / 创建销毁线程 | 错误语义 | 不能复用现有类型的原因 |
| --- | --- | --- | --- | --- | --- |
| `GlobalShaderType` | 借用 UE Global Shader Type 术语，但在 Toy3d 标识一个完整多 stage Program contract | 静态只读值或调用方按值引用；不拥有 Program、loader 或 RHI object | 不可变，可被 GT/RT 读取 | 自身不存错误；type validation 由 map 构建返回诊断 | `ShaderMapProgramKey` 只标识加载键，不表达 Program kind、required stages 和 binding schema |
| `GlobalShaderBindingRequirement` | 一个 Global Shader type 对逻辑 binding 的最小稳定要求，按 parameter identity/group/type/stage 验证而不暴露 native slot | `GlobalShaderType` 按值拥有不可变集合；不拥有 RHI object | 构造后不可变，可被 GT/RT 读取 | 不匹配由 GlobalShaderMap candidate 构建返回诊断 | 现有 `ShaderMapBinding` 是加载产物的完整 target mapping，不能反向作为静态 type requirement，也不能把 native binding 固化到跨 target 声明 |
| `GlobalShaderMap` | 对应 UE GlobalShaderMap 的精简 CPU 领域索引；只组合现有 `ShaderMap` | Engine/GT 持有冻结 map，并以共享不可变引用跨到 Renderer；entries 强持有 `ShaderMapProgram` | 仅 GT 构建；冻结后 GT/RT 只读；device 之前即可销毁，但正常流程晚于 Renderer teardown | candidate 全有或全无；查询未知 type 返回可诊断失败 | 通用 `ShaderMap` 面向任意 Program 和 loader cache，不表达内置 type 集合、启动策略或冻结边界 |
| `GlobalShaderMapResult` | GlobalShaderMap candidate 的成功值或 CPU Shader 诊断 | 临时返回值；成功时转移 map ownership | GT 创建和消费 | 保留 ShaderMap loader/validation 的原始文本诊断和失败 type identity | 现有 `ShaderMapProgramResult` 只返回单个 Program，不能表达全有或全无集合构建 |

## Minimal Implementation Example

```cpp
// GT composition root：platform 来自 backend configuration，而非 Pass。
std::vector<const GlobalShaderType*> required_types;
required_types.push_back(&tonemap_global_shader);
if (enable_imgui)
{
    required_types.push_back(&imgui_global_shader);
}

GlobalShaderMapResult built = GlobalShaderMap::load(shader_map, shader_platform, required_types);
if (!built.succeeded())
{
    return fail_engine_init(built.error());
}
std::shared_ptr<const GlobalShaderMap> frozen_map = built.take_map();

// logical RT：只读查询；RHI 创建交给 device-scoped cache。
ShaderMapProgramRef tonemap = frozen_map->find(tonemap_global_shader);
```
