## Why

当前 Forward Renderer 仍以 swapchain format 创建 `SceneColor`，通过 texture copy 输出到 backbuffer，并只保留一个不绘制内容的 UI attachment scope；这条占位链无法表达 HDR SceneColor、正式 Tonemap，也不能让 Dear ImGui 在不泄漏 Vulkan 类型的前提下成为 Runtime 与未来 Editor 共用的 UI 框架。新的渲染管线骨架已经稳定，现在需要建立偏 UE4.27 职责划分、但适合 Toy3d 单 graphics queue 与跨后端约束的最终输出和 UI 闭环。

## What Changes

- 新增正式 Tonemap 能力：Forward Base Pass 输出固定 HDR `SceneColor`，Tonemap 作为 Global/Pass shader 阶段把 HDR SceneColor 转换到调用方提供的最终 color attachment；第一阶段定义 SDR 输出、手动曝光和确定的 tone curve/output encoding，不包含自动曝光、Bloom、LUT、HDR10 或其他完整后处理链。
- 扩展 `.shader` v1 的 `Parameters` section，使 Renderer-owned Pass 可以声明数值常量；`Resources` 继续只描述纹理、Sampler 与 Buffer，避免把常量和 descriptor resource 混为同一语言概念。
- 删除正常帧中 `SceneColor` 到 backbuffer 的等格式 copy 占位路径，改为 `SceneColor: RenderTarget -> ShaderResourceGraphics`、最终输出目标 `Present -> RenderTarget -> Present` 的显式状态链；必要 format/usage 不受 profile 支持时返回可诊断 `Unsupported`。
- 新增 Dear ImGui 集成，但只使用 ImGui core 的控件、布局、Docking、字体图集和 `ImDrawData` 生成能力；GPU 渲染、shader、pipeline、binding、upload、scissor、draw、同步和资源生命周期全部走 Toy3d RHI，不使用 `imgui_impl_vulkan`、`imgui_impl_dx11`、`imgui_impl_dx12` 或 GLFW renderer backend。
- 将 ImGui context/UI 构建归属 Game/UI side，将深拷贝后的一次性 draw payload ownership 转移到 logical Rendering Thread；禁止跨线程传递 `ImDrawData*`、`ImDrawList*`、native handle 或裸 RHI 指针。
- ImGui 字体 atlas 进入 Renderer bootstrap：由 ImGui core 生成 CPU 像素，Renderer 使用 device-level graphics context 完成 texture upload/transition，并按既有 all-or-nothing 初始化、queue completion 与 command-list payload 保活规则发布。
- Runtime 主 viewport 将 Tonemap 和 ImGui 录制为两个独立 RHI render pass：Tonemap 以覆盖式 `Discard/Store` 输出 backbuffer并结束，存在有效UI draw data时ImGui随后以 `Load/Store` 载入同一backbuffer并alpha blend；无UI时完全跳过ImGui pass，Tonemap结果仍正常提交和present。
- 未来 Editor Scene Viewport 必须允许 Tonemap 输出独立 LDR viewport texture，再由最终 ImGui pass 采样并组合到 Editor backbuffer；因此 Tonemap 不得假定输出一定是 swapchain，ImGui 也不得依赖 `ForwardSceneRenderer`、Tonemap资源或与Tonemap处于同一原生render pass。
- 第一阶段只支持一个原生 OS 主窗口和主 RHI viewport，可在其中使用 ImGui Docking；Dear ImGui multi-viewports、多个 `IWindow`/`RHISurface`/`RHIViewportContext`、HDR UI 独立合成和 Editor 面板本身均为后续能力。
- 第一阶段支持 font atlas texture、`DisplayPos`、`DisplaySize`、`FramebufferScale`、16/32-bit index、`IdxOffset`、`VtxOffset`、clip/scissor 和 `ImDrawCallback_ResetRenderState`；普通用户 render callback 必须拒绝并诊断。任意 `ImTextureID`/`Image()` 纹理在建立稳定的跨线程 UI texture identity 与 registry 前不得以裸指针或 backend descriptor 冒充支持。
- 补齐主窗口 ImGui 交互所需的输入边界，包括鼠标、滚轮、键盘、modifier、文本输入、焦点、DPI/framebuffer scale 和 capture policy；不得让平台消息处理器直接调用图形 backend，也不得另建与 Runtime Input 语义重复的通用输入系统。
- 对现有 Dear ImGui 依赖做一次明确、固定版本的升级和 target 收敛：core target 不再编译或公开传播 Vulkan/GLFW backend 依赖，`Toy3dRuntime` 仅以实现依赖消费 ImGui。
- 保持现阶段一个 viewport frame、一个 graphics context、一个 immutable business command list 和一次 `end_frame()` 的提交模型；不引入 RDG、通用临时 Pass Scheduler、隐藏 submit、逐帧 wait、后端判断或新的公共 RHI 图形命令。

## Capabilities

### New Capabilities

- `game-render-framework/final-output`: 定义 HDR SceneColor、Tonemap、最终输出目标、Runtime独立Tonemap/ImGui pass及其Load/Store顺序、Editor离屏输出兼容性、颜色输出边界及三后端/移动profile行为。
- `game-render-framework/imgui-rendering`: 定义 ImGui core 与 Toy3d RHI 的分层、跨线程 draw payload、字体 bootstrap、RHI draw contract、纹理身份限制、失败行为以及 Runtime/Editor 共用边界。
- `game-render-framework/imgui-input`: 定义主窗口 ImGui 输入投递、文本与焦点、DPI/framebuffer scale、capture policy 和平台/Input/UI 分层。

### Modified Capabilities

- `game-render-framework`: 将端到端帧顺序从 Forward Base Pass 后直接提交扩展为 HDR SceneColor、Tonemap、可选 ImGui、业务 submit 与 presentation，并保持 CPU Fence、submit 和 GPU completion 语义分离。
- `game-render-framework/view-render-flow`: 将 Forward SceneRenderer 的正式 pass 录制边界扩展到最终输出，明确一次性 UI payload 与同一 graphics list 的顺序，同时保留 Runtime 快速路径和 Editor 离屏 viewport 路径。
- `game-render-framework/engine-composition-root`: 在主循环和 composition root 中加入 UI context/input/frame 构建与 draw payload ownership transfer，但不让 Engine 成为 ImGui、Renderer 或 RHI service locator。
- `game-render-framework/renderer-bootstrap`: 将必需的 Tonemap/ImGui shader 与 font atlas RHI resources 纳入 Renderer-owned、device-level bootstrap 和失败回滚 contract。
- `game-render-framework/renderer-terminal-shutdown`: 将 UI draw payload、font resources、动态 UI buffers 和相关 non-owning 记录纳入 terminal disposal、completion-driven retirement 与 device 前释放顺序。

## Impact

- 受影响模块包括 `engine/runtime/renderscene/` 的 frame orchestration、`SceneRenderTargets`、Forward Renderer 与新增 post-process/UI pass，`engine/runtime/rendercore/shader/` 的 built-in/global program 获取路径，`engine/runtime/input/` 和各平台输入适配，以及 `engine/runtime/engine.*`/Application 的 UI frame 编排边界。
- 新增内建 Tonemap 与 ImGui shader 资产及对应 ShaderMapEntry/ShaderCodeLibrary 构建输入；renderscene 不读取裸 shader binary，也不手写 target-native binding。
- Shader compiler frontend、ToyShaderABI layout、generated HLSL、reflection 与测试会增加 `Parameters { Pass { ... } }` 支持；第一阶段不开放自定义 Global/View/Material/Object 常量 schema、数组、struct 或原生 cbuffer/register/set。
- `engine/thirdparty/imgui` 的版本和 CMake target 将作为依赖升级的一部分收敛；第三方 core 源码不承载 Toy3d RHI 或平台策略。
- 公共 RHI 预计无需新增命令：现有 render pass、graphics pipeline、Pass binding、buffer/texture upload、viewport/scissor、`draw()`、`draw_indexed()` 和 transition 能表达第一阶段闭环；若实现发现缺口，必须先重新评估 Vulkan、D3D11、D3D12 与 VulkanPortable v1，再单独修改 RHI contract。
- Runtime 第一阶段继续以 Vulkan 实现闭环，但 shader、resource、binding、load/store、draw 和错误语义必须可映射到 D3D11 FL11_0/SM5、D3D12 和移动端 Vulkan profile；不得把 Vulkan-Samples 的 native descriptor、push constant、surface transform 或 command-buffer ownership带入公共层或 renderscene。
- 本 change 为未来 Editor 提供可复用基础，但不实现 Editor 面板、资产浏览器、Scene/Game/Material 多 viewport、任意 UI texture registry、OS 多窗口或 Dear ImGui multi-viewports。
