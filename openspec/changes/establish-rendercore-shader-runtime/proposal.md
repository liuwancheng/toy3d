## Why

当前内置 Tonemap/ImGui pass 由 Engine 按字符串逐项加载并注入 `ShaderMapProgram`，具体 pass 又重复验证 Shader identity/platform，Forward Base Pass 还在每帧重建 RHI shader program，并在 `forward_scene_renderer.cpp` 内维护 Shader format 到 RHI graphics state 的转换。这些入口把进程级 CPU Shader 索引、内置 Shader 领域、device-scoped RHI 对象和具体 pass 策略混在一起，继续增加内置 pass 或 D3D11/D3D12 后端会复制加载、验证、缓存和平台分支。

本 change 先建立 RenderCore Shader runtime 的长期边界，使后续 Global Shader pass 与 Forward Mesh Pass 都复用同一条 CPU Program→RHI Program 路径，同时保持显式 Renderer、单 graphics context/list 和现有 RHI contract 不变。

## What Changes

- 在现有进程级 `ShaderMap` 之上建立领域化 `GlobalShaderMap`，使用显式 `GlobalShaderType` 描述内置 Shader Program 的 identity、permutation、required stages 和 binding schema，并由 composition root 的加载计划表达 required/optional 启动策略。
- 明确 Toy3d 的 `GlobalShaderType` 对应当前多 stage `ShaderMapProgram`，不复制 UE4.27 的单 stage 类型、注册宏或全局可变 registry。
- 将 Vulkan Shader platform/profile 标识统一命名为 `VulkanES31`，对应文档术语为 `Vulkan ES3.1 profile`；其 contract 仍固定为 Vulkan 1.1、SPIR-V 1.3 与最多四个 bound descriptor sets。该重命名保持持久化枚举数值不变，且不保留 `VulkanPortableV1` compatibility alias。
- 建立 RT-only、device-scoped `RHIShaderProgramCache`，按完整稳定 Program identity/content/layout 缓存 `RHIShaderProgram`，供 Global Shader 与 Material Shader 共用。
- 将 `ShaderGraphicsPassState` 到 `RHIGraphicsPipelineDesc` 的公共转换和诊断迁入 RenderCore Shader adapter，删除 `ForwardSceneRenderer` 私有的枚举翻译与静默默认回退。
- 将 Tonemap 和可选 ImGui Shader 迁移到 `GlobalShaderMap`；具体 pass 不再硬编码 Shader name/pass name、`ShaderPlatform::VulkanES31` 或直接创建 RHI shader program。
- **BREAKING**：Renderer 启动输入由逐项 `tonemap_program`/`imgui_program` 参数改为单个冻结的 `GlobalShaderMap`，新增内置 pass 不再扩张 Renderer 构造签名。
- composition root 仍在 GT 选择与 backend 配置一致的 `ShaderPlatform` 并完成 CPU Shader I/O；logical RT 只基于不可变 Program 创建/cache RHI 对象，公共 RHI 不依赖 `ShaderPlatform`。
- 本 change 不包含 Base Pass/`MeshDrawCommand` 重构、`MaterialShaderMap`、RenderCore/RenderScene 目录大迁移、CMake target 拆分、SceneRenderTargets resize、RDG 或通用 Pass Scheduler。

## Capabilities

### New Capabilities

- `rendercore-shader-programs`: 定义 Shader graphics state adapter、device-scoped RHI Shader Program cache、稳定 cache identity、线程/所有权、失败和销毁行为。
- `global-shaders`: 定义 `GlobalShaderType`、`GlobalShaderMap`、显式注册/加载、required/optional Shader、平台选择、Pass 查询与 Tonemap/ImGui 迁移 contract。

### Modified Capabilities

- `game-render-framework/renderer-bootstrap`: Renderer 启动输入改为冻结的 GlobalShaderMap，并在 logical RT 建立、使用和逆序释放 RHIShaderProgramCache；必需 Global Shader 的创建失败纳入全有或全无启动语义。
- `game-render-framework/engine-composition-root`: Engine 在 GT 选择 ShaderPlatform、加载并冻结 GlobalShaderMap，再把不可变输入交给 Renderer；Engine 仍不得拥有或调用 RT 可变 RHI cache。
- `game-render-framework/renderer-terminal-shutdown`: Renderer-owned Shader/Pass RHI refs 与 RHIShaderProgramCache 必须在 viewport/device 之前按健康或 DeviceLost 路径释放，且不得覆盖 first terminal error。

## Impact

- 主要影响 `engine/runtime/rendercore/shader/`、`engine/runtime/renderscene/postprocess/`、`engine/runtime/renderscene/ui/`、`engine/runtime/renderscene/renderer.*` 与 `engine/runtime/engine.*`。
- 新增 RenderCore Shader runtime 测试，并更新 Renderer bootstrap、Tonemap、ImGui 和 Forward Base Pass 相关测试；代码完成后需要配置、构建 `Toy3dEditor` 和受影响测试目标，并运行对应 CTest。
- 不修改 ShaderMapEntry 持久化格式、`.shader` 语言、公共 RHI descriptor、Vulkan/D3D native mapping或现有 command-list/resource-state contract。
- Vulkan、D3D11、D3D12 与 Vulkan ES3.1 profile 使用相同 RenderCore 生命周期和 cache 语义；后端差异只体现在 composition root 选择的 ShaderPlatform 产物以及现有 `RHIDevice` 原生创建实现。
