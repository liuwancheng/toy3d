## MODIFIED Requirements

### Requirement: Bootstrap 顺序固定
RT启动引导 MUST按RHIDevice、`RHIShaderProgramCache`、RenderResourceManager、Required Global Shader RHI Programs与Pass resources、placeholder device submission、exact completion wait、primary viewport的顺序执行，全部成功后才发布Renderer Running。Renderer MUST先从Stopped发布Starting；成功后发布Running并开放普通Scene/resource/frame façade，失败则发布Terminal/RendererStatus first error并保持façade关闭。

Renderer 构造输入 MUST 使用单个已冻结 `GlobalShaderMap`，不得继续接收逐项 `tonemap_program`、`imgui_program` 或以后每个内置 Pass 的 Program 参数。Tonemap 始终 required；ImGui 只有在 GT 已启用并提交 font atlas 时 required。任何 required type 查询、RHI Program 创建或 Pass resource 初始化失败 MUST 使启动全有或全无失败。

#### Scenario: 正常启动
- **WHEN** Window/Surface、Task Graph、RenderingThread ready 且冻结 GlobalShaderMap 包含本次启用的 required types
- **THEN** bootstrap MUST 完成 required RHI Programs、Pass resources、placeholder GPU 可用性和 viewport 创建后才开放 frame/resource façade

#### Scenario: Required Global Shader 创建失败
- **WHEN** Tonemap 或已启用 ImGui 的 Global Shader 查询、RHI Program 创建或 Pass resource 初始化失败
- **THEN** Renderer MUST保留原始错误、逆序释放未发布 refs、保持普通 façade 关闭且不进入 Running

#### Scenario: 普通命令过早到达
- **WHEN** RenderingThread已经attach但Renderer仍处于Starting且启动结果尚未发布
- **THEN** 普通Scene/resource/frame façade MUST拒绝enqueue或保持关闭，不得让命令观察部分初始化domain

### Requirement: 启动引导输入与线程固定
composition root SHALL在GT创建Window和backend-independent RHISurface输入，选择 `ShaderPlatform`，完成 `ShaderMap` CPU I/O并发布冻结 `GlobalShaderMap`，再启动Task Graph/RenderingThread ready handshake。RHIDevice、`RHIShaderProgramCache`、RHI Shader Programs、Manager、Pass resources、placeholder、viewport的创建和全部可变状态 MUST只在logical RT执行。bootstrap result必须在普通façade开放前发布给composition root，GT不得轮询RT内部对象判断ready，也不得拥有或调用 RHI Program cache。

#### Scenario: Surface 已创建
- **WHEN** logical RT开始Renderer Starting流程
- **THEN** 它 MAY消费composition root提供的RHISurface identity与冻结 GlobalShaderMap 创建device和RHI Programs，但不得读取Window可变 internals、执行 Shader 文件 I/O 或改变 GlobalShaderMap

#### Scenario: CPU Shader 加载失败
- **WHEN** GT 构建 required GlobalShaderMap candidate 失败
- **THEN** RenderingThread/Renderer bootstrap MUST不启动，Engine按其初始化阶段逆序回滚并保留 Shader 诊断

## ADDED Requirements

### Requirement: Program cache 与 Pass candidate 不部分发布
Required Global Shader RHI Programs和对应 Pass resources MUST先作为 bootstrap candidate 构建。只有每项成功后才可发布给普通 frame；任一步失败时，Pass candidate refs MUST先释放，随后清空 `RHIShaderProgramCache` entries，再继续已有 Renderer bootstrap 回滚。成功缓存的其他 Program不得使失败的 Pass看起来可用。

#### Scenario: ImGui 成功而 Tonemap 失败
- **WHEN** ImGui Program candidate 已创建但 Tonemap Program 创建失败
- **THEN** 两者都 MUST不发布，ImGui candidate ref 与 cache refs MUST按 device 存活顺序释放，Renderer不进入 Running

#### Scenario: Cache hit during bootstrap
- **WHEN** 两个 required Pass 使用同一完整 Program identity
- **THEN** bootstrap MUST通过一个 device cache 复用 RHI Program，但仍分别验证和发布各自 Pass resources

## Type Contracts

本 capability 不新增独占具名类型。`GlobalShaderMap`、`RHIShaderProgramCache` 及相关值类型分别由 `global-shaders` 与 `rendercore-shader-programs` capability 唯一登记；Renderer 只组合这些类型。

## Minimal Implementation Example

```cpp
// GT owner：冻结 map 的共享只读引用跨过 Renderer 稳定外壳。
Renderer renderer(task_graph, surface, viewport_desc, device_factory, global_shader_map, font_atlas);

// logical RT：任何失败都走现有 first-error bootstrap 回滚。
device = device_factory();
RHIShaderProgramCache program_cache(*device);
auto tonemap_cpu = global_shader_map->find(tonemap_global_shader);
auto tonemap_rhi = program_cache.find_or_create(tonemap_cpu);
if (!tonemap_rhi.succeeded())
{
    return fail_startup(tonemap_rhi.status());
}
// required candidates、placeholder completion、viewport 全部成功后才发布 Running。
```
