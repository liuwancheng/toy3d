## MODIFIED Requirements

### Requirement: Engine 拥有进程级框架对象
现有 `toy3d::Engine` SHALL 在 Game Thread 显式拥有 Platform、Window/RHISurface、Task Graph active instance、ShaderMap loader、进程级 `ShaderMap`、冻结 `GlobalShaderMap`、Renderer 稳定外壳、RenderingThread lifecycle controller 与 FrameEndSync，并按依赖逆序销毁。Renderer 稳定外壳 MAY 由 Engine 持有，但其 RT 可变内部对象、`RHIDevice` 与 `RHIShaderProgramCache` MUST 只在 logical Rendering Thread 创建、访问和销毁。

#### Scenario: Engine 初始化
- **WHEN** Engine 开始完整 runtime 初始化
- **THEN** 它 MUST 依次完成日志/文件系统/配置、Platform、Window/RHISurface、backend对应ShaderPlatform选择、ShaderMap loader/map与GlobalShaderMap加载冻结、Task Graph与GameThread attach、Renderer稳定外壳、RenderingThread start和Renderer启动引导，全部成功后才进入主循环

#### Scenario: Renderer shell 已创建但 RT 尚未 ready
- **WHEN** Engine 已持有 Renderer 稳定外壳但 RenderingThread 尚未完成 attach 和启动引导
- **THEN** 普通 Scene/resource/frame façade MUST 保持关闭，Engine 不得通过稳定外壳读取或修改尚未发布的 RT 内部状态

#### Scenario: GlobalShaderMap 存活
- **WHEN** Renderer 在 logical RT 初始化或运行中查询内置 Program
- **THEN** Engine-owned 冻结 GlobalShaderMap MUST保持存活且不可变，直到 Renderer teardown 完成

### Requirement: RT 可变状态不属于 Engine
Engine MUST NOT 直接拥有或调用 RHIDevice、RHIViewportContext、`RHIShaderProgramCache`、RHI Shader Program、RenderScene、RenderResourceManager、placeholder resources或具体SceneRendering的可变执行接口；这些对象 MUST在logical Rendering Thread的Renderer domain内创建和销毁。Engine 只可拥有 Shader loader、进程级 `ShaderMap` 与冻结 `GlobalShaderMap` 的 CPU 状态。

#### Scenario: 每帧绘制
- **WHEN** Engine main loop 请求一帧渲染
- **THEN** Engine MUST通过Game/Render framework投递一次性frame/view输入与Draw，而不得直接begin/end RHI frame、查询或创建RHI Shader Program、调用render pass、提交command list或查询RenderScene

#### Scenario: Global Shader CPU I/O
- **WHEN** Engine 准备启动 Renderer
- **THEN** 它 MUST在GT完成所选ShaderPlatform的GlobalShaderMap加载冻结，再把只读引用交给Renderer，不得把loader或可变ShaderMap操作推迟到logical RT

### Requirement: 初始化失败逆序回滚
Engine MUST 保留首个初始化错误，只清理已经成功创建的阶段，并按 Renderer domain、RenderingThread、Task Graph、冻结GlobalShaderMap、ShaderMap/loader、Window/RHISurface、Platform 的依赖逆序回滚。若失败发生在 RenderingThread 启动之前，只清理已存在的 CPU Shader 与平台阶段。回滚完成前不得进入主循环或发布可运行状态；cleanup error 只能作为 secondary diagnostic，不得覆盖原始错误。

#### Scenario: GlobalShaderMap 加载失败
- **WHEN** required Global Shader Program 缺失、损坏或与所选 ShaderPlatform 不匹配
- **THEN** Engine MUST不创建Renderer/RenderingThread，释放ShaderMap/loader与后续平台阶段，并返回原始Shader诊断

#### Scenario: Renderer 启动引导失败
- **WHEN** Task Graph 与 RenderingThread 已启动但 Renderer 启动引导失败
- **THEN** Engine MUST让logical RT回滚Renderer domain、request return并join RenderingThread、shutdown Task Graph，再释放冻结GlobalShaderMap与ShaderMap/loader、销毁Window/RHISurface与Platform，并返回原始错误

#### Scenario: RenderingThread attach 失败
- **WHEN** Task Graph 已创建但 RenderingThread 无法 attach
- **THEN** Renderer 启动引导 MUST不执行，Engine MUST回收部分线程状态、shutdown Task Graph、释放CPU Shader objects并销毁后续不再需要的Window/RHISurface与Platform

## ADDED Requirements

### Requirement: Backend 与 ShaderPlatform 映射集中
Engine composition root SHALL 使用一个明确、可测试的 backend configuration→`ShaderPlatform` 映射选择启动 Program。Tonemap、ImGui、Renderer 与其他 RenderScene 代码不得覆盖该选择或依据操作系统猜测 platform。未启用 backend、backend 尚无匹配 Shader target 或 runtime package profile 不兼容时 MUST在启动阶段可诊断失败。

#### Scenario: Vulkan backend
- **WHEN** Engine configuration 选择符合移动基线的 Vulkan backend
- **THEN** Engine MUST统一选择 `ShaderPlatform::VulkanES31` 并用它构建全部 Global Shader requests

#### Scenario: 未实现的 D3D11 backend
- **WHEN** configuration 请求 D3D11 而当前 build 无对应 backend或SM5 Shader产物
- **THEN** 初始化 MUST返回明确 Unsupported/配置错误，不得回退到 Vulkan Shader Program

## Type Contracts

本 capability 不新增独占具名类型。`ShaderPlatform`、`ShaderMap` 为现有 Shader runtime 类型，`GlobalShaderMap` 由 `global-shaders` capability 登记；Engine 只承担 composition ownership。

## Minimal Implementation Example

```cpp
// GT：唯一一次 backend→ShaderPlatform 决策与 CPU I/O。
ShaderPlatform shader_platform = select_shader_platform(runtime_backend);
GlobalShaderMapResult loaded = GlobalShaderMap::load(shader_map, shader_platform, select_required_global_shader_types(enable_imgui));
if (!loaded.succeeded())
{
    return rollback_before_rendering_thread(loaded.error());
}

global_shader_map = loaded.take_map();
renderer = std::make_unique<Renderer>(/* stable inputs */, global_shader_map, font_atlas);
// logical RT bootstrap 失败时先拆 Renderer domain；GT join 后才释放 global_shader_map。
```
