## Purpose

建立 RenderCore 中从已验证 CPU Shader Program 到 device-scoped RHI Shader Program 的唯一运行时边界，使内置 Pass 与后续 Material 路径共享一致的状态转换、缓存、线程、失败和销毁语义。

## ADDED Requirements

### Requirement: Shader graphics state 由 RenderCore 统一转换
RenderCore SHALL 提供从已验证 `ShaderGraphicsPassState` 与当前 attachment/vertex input 补充信息构造完整 `RHIGraphicsPipelineDesc` 的唯一 adapter。RenderScene pass MUST 只提供自身决定的 attachment compatibility、vertex input 和动态状态，不得自行翻译 primitive topology、rasterizer、depth/stencil、blend 或 color write 枚举。

adapter MUST 覆盖 Shader format 中的全部合法 graphics state；遇到未知值、非法组合或 compute Program 时 MUST 返回可诊断失败，不得选择默认 topology、关闭 depth/blend 或用数值强制转换继续执行。公共 RHI 和各 backend MUST NOT 依赖 Shader format 类型。

#### Scenario: 合法 graphics state
- **WHEN** 调用方提交已验证的 graphics Program state、兼容的 vertex input 与 attachment 信息
- **THEN** adapter MUST 生成语义完整且可由公共 RHI 再验证的 `RHIGraphicsPipelineDesc`

#### Scenario: 非法 topology
- **WHEN** Program state 包含 RenderCore 无法映射的 primitive topology
- **THEN** adapter MUST 返回包含 shader/pass identity 与字段名的失败，且不得调用 RHI pipeline 创建

### Requirement: RHI Shader Program 按 device identity 缓存
每个已初始化 `RHIDevice` SHALL 对应一个 RenderCore-owned `RHIShaderProgramCache`。Global Shader 与后续 Material Shader MUST 通过该 cache 把不可变 `ShaderMapProgram` 解析为 `RHIShaderProgram`；RenderScene pass、Mesh draw 或逐帧路径不得直接重复创建同一 Program 的 stage shader、binding layout 或 RHI program 聚合。

cache key MUST 覆盖 Program 的稳定逻辑 identity、permutation、mapping version、每个 stage 的种类、entry point、content hash，以及 logical layout 与 target binding layout identity；debug name 和 CPU/RHI 对象地址 MUST NOT 参与 identity。hash 命中后 MUST执行完整 equality，不能假定 hash 唯一。

#### Scenario: 两个 Pass 请求同一 Program
- **WHEN** 同一 device 上 Tonemap 初始化和另一调用方以等价完整 identity 请求相同 `ShaderMapProgram`
- **THEN** 两者 MUST 获得同一缓存 RHI Program 强引用，backend shader/layout 创建只发生一次

#### Scenario: 字节码相同但 binding layout 不同
- **WHEN** 两个 Program 的 stage content hash 相同但 target binding layout identity 不同
- **THEN** cache MUST 把它们视为不同 Program，不得返回错误 layout 的缓存对象

#### Scenario: hash collision
- **WHEN** 两个不相等的 Program key 产生相同容器 hash
- **THEN** equality check MUST 阻止错误复用并分别创建或查找对应 Program

### Requirement: Cache 创建失败可诊断且不做 negative cache
Program validation、RHI shader 创建、binding layout 创建或 Program 聚合任一步失败 MUST 返回原始 `RHIErrorCode` 与包含 Program identity、stage 和失败阶段的诊断。失败 candidate MUST NOT 发布到 cache，已创建的未发布 RHI refs MUST 立即按正常强引用规则释放；后续同一请求 MAY 重试。

cache MUST NOT 把失败永久记为命中，也不得把 `Unsupported`、`OutOfMemory`、`DeviceLost` 或 `BackendFailure` 改写为统一错误。若 device 已进入 terminal/shutdown，cache MUST 拒绝新请求并保留 device 的原始状态诊断。

#### Scenario: Pixel shader 创建失败
- **WHEN** vertex shader 已创建而 pixel shader 创建返回 `Unsupported`
- **THEN** cache MUST 释放未发布 vertex shader ref、返回原始 `Unsupported`，并且不得留下可命中的部分 Program

#### Scenario: 失败后重试
- **WHEN** 一个非 terminal 创建请求失败且调用方之后再次请求相同 Program
- **THEN** cache MAY 再次执行完整创建，不得直接返回旧失败记录

### Requirement: Cache 只在 logical Rendering Thread 可变
`RHIShaderProgramCache` MUST 在 logical RT 创建、查询和销毁；GT 只能传递不可变 `ShaderMapProgram` 或 `GlobalShaderMap` 引用，不得调用 cache。cache 不拥有 `RHIDevice`，仅在 Renderer 保证 device 存活期间持有其 non-owning reference；cache 中成功发布的 RHI refs MUST 在 device shutdown 前释放。

本阶段不承诺并发 miss 或跨 device 共享。若未来允许 Pass 间并行录制，必须先为 cache 增加明确同步或在启动阶段冻结所需 entries，不能依赖当前单线程实现的偶然安全性。

#### Scenario: Renderer 启动
- **WHEN** logical RT 已成功初始化 device
- **THEN** Renderer MUST 在第一个 Global Shader RHI 请求前创建该 device 的 cache

#### Scenario: Renderer 销毁
- **WHEN** Renderer 开始正常或 terminal teardown
- **THEN** 所有 Pass-owned Program refs 与 cache entries MUST 在 device 销毁前于 logical RT 释放

### Requirement: 三后端共享同一 RenderCore contract
Vulkan、D3D11 Feature Level 11_0/SM5、D3D12/SM6 与 `Vulkan ES3.1 profile` SHALL 使用相同的 Program cache identity、调用顺序和错误 contract。RenderCore 与 RenderScene MUST NOT 按 backend 名称分支；target binary 和 mapping 的差异只能来自 GT 选择并由 `ShaderMap` 验证的 `ShaderPlatform` Program，以及现有 RHI backend 实现。

#### Scenario: D3D11 Program
- **WHEN** cache 消费已验证的 D3D11 SM5 Program
- **THEN** 它 MUST 通过公共 `RHIDevice` 创建入口完成 Program，不得要求 RenderScene 提供 D3D register 或原生对象

#### Scenario: Vulkan ES3.1 Program
- **WHEN** cache 消费 `Vulkan ES3.1 profile` Program
- **THEN** 四 physical set mapping MUST 继续由现有 Shader/RHI contract 验证，cache 不得把 descriptor set 暴露给调用方

## Type Contracts

| 类型 | UE4.27 术语参考与 Toy3d 职责 | 所有权与生命周期 | 可变线程 / 创建销毁线程 | 错误语义 | 不能复用现有类型的原因 |
| --- | --- | --- | --- | --- | --- |
| `RHIShaderProgramKey` | 对应 UE RenderCore 中 shader resource identity 的精简值语义；完整标识一个可在同一 device 上复用的 RHI Program | cache entry 内按值持有，不拥有 CPU/RHI 对象 | 构造后不可变；logical RT 构造和消费 | 无独立错误；由 key 构造前的 Program validation 报错 | `ShaderMapProgramKey` 只覆盖加载 identity，不覆盖 stage content、entry point、mapping/layout，不能安全标识 RHI object |
| `RHIShaderProgramCache` | RenderCore 的 device-scoped shader resource cache；解析 `ShaderMapProgram` 并发布完整 `RHIShaderProgram` | Renderer 独占；non-owning 引用一个活着的 `RHIDevice`，entries 强持有 RHI refs | 仅 logical RT 可变并在该线程创建/销毁 | 返回原始 `RHIStatus`；失败不发布、不做 negative cache | `ShaderMap` 是进程级 CPU Program/I/O cache，不拥有 device，也不能承载 RHI 生命周期；RHIDevice 内部 cache 不应反向依赖 Shader format/ShaderMap |

## Minimal Implementation Example

```cpp
// Engine/GT 已完成 ShaderMap I/O；此引用之后只读。
ShaderMapProgramRef cpu_program = global_shader_map.find(tonemap_global_shader);

// logical RT：Renderer 保证 device 比 cache 和所有返回 ref 活得更久。
RHIShaderProgramCache program_cache(*device);
auto program_result = program_cache.find_or_create(cpu_program);
if (!program_result.succeeded())
{
    // 保留原始 RHI code；bootstrap 不发布 Running。
    return fail_startup(program_result.status());
}

std::shared_ptr<const RHIShaderProgram> tonemap_program = program_result.value();
// teardown：先释放 pass ref，再清 cache，最后 shutdown/destroy device。
```
