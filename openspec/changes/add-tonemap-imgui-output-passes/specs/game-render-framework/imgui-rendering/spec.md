## Purpose

定义 Dear ImGui core 生成的 UI 数据如何在 Game/UI side 构建、以一次性不可变 payload 传入 logical Rendering Thread，并完全通过 Toy3d RHI 完成字体、buffer、binding、裁剪、绘制、提交和生命周期管理，从而供 Runtime 与未来 Editor 共用。

## Type Contracts

| 类型 | 性质与职责 | 所有权与线程 | 错误语义与复用理由 |
| --- | --- | --- | --- |
| `ImGuiTextureId` | 新增整数 value identity；标识 UI draw command 请求的逻辑纹理，第一阶段只定义 invalid 与 font atlas identity，不保存指针或 native descriptor | 按值跨 GT/RT；identity 的解析只在 logical RT ImGui renderer 内发生 | 未注册 identity 使本帧 UI payload validation 失败；不能复用裸 `ImTextureID`，因为其默认 pointer 形状不表达跨线程所有权 |
| `ImGuiVertex` | 新增 POD value type；保存 position、UV 与 packed color 的稳定 CPU/GPU vertex contract | GT snapshot 按值创建，move 到 RT，上传后随 payload 释放 | layout/size 由测试固定；不能把第三方 `ImDrawVert` object representation直接作为长期第一方接口 |
| `ImGuiDrawCommand` | 新增 value type；保存 element count、clip rect、texture identity、index/vertex offset 与 reset-state 标记 | GT snapshot 创建，logical RT 只读 | 越界、非有限 clip 或不支持 callback在 snapshot validation 失败；不能保存 `ImDrawCmd*` 或 callback pointer |
| `ImGuiDrawData` | 新增 move-only frame payload；拥有扁平化 vertices、indices、draw commands、display position/size 和 framebuffer scale | `ImGuiSystem` 在 GT 创建，整体 move 进 Draw command，在 logical RT执行或 terminal disposal 路径析构 | validation 失败不发布 payload且本帧场景仍可继续；不能复用 `ImDrawData`，因为其内部 pointer只在下一次 ImGui frame前有效 |
| `ImGuiFontAtlasData` | 新增 immutable CPU bootstrap payload；拥有 RGBA font pixels、extent 和 font texture identity | GT ImGui context生成并复制；Renderer bootstrap在logical RT消费，成功或失败后释放CPU副本 | 非法 extent/pitch或空像素导致启用ImGui的bootstrap失败；不能让Renderer读取GT ImGui atlas pointer |
| `ImGuiSystem` | 新增 GT-only owner；显式拥有一个 ImGui context，接收输入、开始/结束 UI frame并构造已验证 `ImGuiDrawData`/font atlas payload | Engine composition root在GT创建销毁；不拥有RHI、RenderScene或backend object | UI snapshot局部错误通过UI诊断返回并允许场景帧无UI继续；不能使用不可替换全局singleton，因为context/lifecycle必须由composition root控制 |
| `ImGuiRenderer` | 新增 RT-only owner；持有font texture/view/sampler、RHI shader/binding/pipeline和completion-aware vertex/index buffer pool，并录制ImGui逻辑阶段 | Renderer domain在logical RT bootstrap创建、teardown销毁；command list保活当前draw实际引用到queue completion | bootstrap必要资源失败使启用ImGui的Renderer启动失败；frame RHI录制失败返回原始RHI错误并触发整帧abort；不能复用Material或RenderResourceManager，因为UI动态frame buffers不是Asset representation |

## ADDED Requirements

### Requirement: 只使用 ImGui core 生成绘制数据
Dear ImGui SHALL只负责 context、控件状态、布局、Docking、字体 atlas 和 CPU draw-data generation。Toy3d MUST自行负责 graphics API resource、shader、pipeline、binding、buffer upload、render pass、draw、同步和销毁；正式 Runtime target MUST不编译或调用官方 Vulkan、D3D11、D3D12 或 GLFW renderer backend。

#### Scenario: Vulkan Runtime
- **WHEN** Vulkan backend 渲染 ImGui
- **THEN** renderscene/UI代码 MUST只调用 Toy3d RHI，不能接收或获取 `VkDevice`、`VkRenderPass`、`VkDescriptorSet` 或 `VkCommandBuffer`

### Requirement: UI frame payload 深拷贝并一次性转移
`ImGui::Render()` 后的有效 draw data MUST在 GT 被验证并深拷贝为拥有全部 vertex、index、command 和 display metadata 的一次性 payload，再随本帧 Draw ownership move 到 logical RT。payload MUST不引用 ImGui arena、窗口、Application、World 或下一帧可变状态。

#### Scenario: GT 开始下一 UI frame
- **WHEN** frame N payload 已投递且 GT 调用 frame N+1 的 `NewFrame()`
- **THEN** logical RT 仍 MUST能完整渲染 frame N，不得读取已被 ImGui core 重用的内存

#### Scenario: Draw 被 terminal skip
- **WHEN** Renderer terminal发生在包含UI payload的Draw执行前
- **THEN** payload MUST在logical RT disposal路径安全析构，不得调用 ImGui 或 RHI

### Requirement: Font atlas 使用 Renderer bootstrap
启用 ImGui 时，font atlas MUST由 GT context生成 immutable RGBA payload，并由 logical RT Renderer 使用显式 device-level context 创建空 texture、upload、transition、finish、submit和等待指定completion。font texture/view/sampler及对应shader resources全部成功后才能发布 ImGui renderer ready。

#### Scenario: Font upload 成功
- **WHEN** font atlas submission 达到指定 completion
- **THEN** font texture identity MUST可被后续UI draw解析为有效RHI texture view/sampler，普通帧不得再次同步等待该bootstrap路径

#### Scenario: Font bootstrap 失败
- **WHEN** font texture创建、upload、transition、submit或completion wait任一步失败
- **THEN** 启用ImGui的Renderer MUST启动失败并保留原始诊断，不得发布无字体或空纹理的可运行UI

### Requirement: ImGui draw contract 完整
ImGui rendering MUST支持 position/UV/packed-color vertex、16-bit或32-bit index、`DisplayPos`、`DisplaySize`、`FramebufferScale`、每命令 `IdxOffset`/`VtxOffset`、texture identity、clip rect和 `ImDrawCallback_ResetRenderState`。clip rect MUST按 `(ClipRect - DisplayPos) * FramebufferScale` 转换、裁剪到 framebuffer并跳过空区域；不得由上层 shader手写 Vulkan Y翻转或surface rotation。

#### Scenario: High-DPI clipped draw
- **WHEN** framebuffer scale不为1且clip rect部分位于framebuffer之外
- **THEN** RT MUST生成裁剪后的整数scissor并仅绘制有效区域，Vulkan与D3D输出位置 MUST等价

#### Scenario: 大于64K顶点
- **WHEN** draw data使用16-bit index且命令包含非零 `VtxOffset`
- **THEN** RHI indexed draw MUST组合command与global offsets正确寻址，不得截断或忽略vertex offset

#### Scenario: Reset render state callback
- **WHEN** draw stream包含 `ImDrawCallback_ResetRenderState`
- **THEN** ImGui renderer MUST重新绑定其pipeline、viewport、vertex/index buffers和graphics bindings后继续后续draw

### Requirement: 第一阶段纹理与 callback 边界明确
第一阶段合法UI texture identity MUST至少包含font atlas；任意其他 `ImTextureID` 在正式registry capability建立前 MUST被拒绝并诊断，不能解释为裸RHI/backend指针。除 reset-state sentinel外的用户render callback MUST在GT snapshot validation拒绝，callback函数和userdata不得跨线程传递。

#### Scenario: 未注册 Image texture
- **WHEN** UI draw command引用非font且未注册的texture identity
- **THEN** 本帧UI payload MUST不发布并记录可诊断错误，场景/Tonemap frame MAY在无UI模式继续

#### Scenario: 普通用户 callback
- **WHEN** ImGui draw list包含普通用户render callback
- **THEN** snapshot MUST拒绝整个UI payload且不得在GT或RT误调用callback，场景帧 MAY继续

### Requirement: 动态 UI buffer 按 GPU completion 复用
ImGui vertex与index数据 MUST通过当前frame graphics context的显式upload路径进入具有对应usage的RHI buffers。可复用buffer/page在关联business submit的queue completion达到前 MUST不被CPU重写、销毁或分配给后续frame；frame abort或明确submit failure MUST不发布其in-flight completion ownership。

#### Scenario: 连续增长的UI数据
- **WHEN** frame N所需容量超过当前可用buffer而旧buffer仍in flight
- **THEN** renderer MUST分配或选择另一安全buffer并让旧command list继续保活原buffer，不能wait idle或覆盖旧内容

#### Scenario: Frame abort
- **WHEN** UI upload已录制但最终frame abort
- **THEN** upload/list local state MUST丢弃，buffer pool MUST按未提交路径恢复可用性且不得伪造completion

### Requirement: ImGui 可独立于 SceneRenderer 输出
ImGui renderer MUST能向调用方提供的合法color attachment录制独立pass，不得依赖ForwardSceneRenderer、SceneColor或Tonemap资源。Runtime MUST在Tonemap pass结束后以 `Load/Store` ImGui pass载入backbuffer；未来Editor MUST能在没有World/Scene draw的frame中单独输出完整Editor UI。

#### Scenario: 空场景Editor frame
- **WHEN** Editor没有有效World或Scene View但存在有效UI draw data
- **THEN** ImGui renderer MUST能够清理/加载Editor backbuffer并绘制UI，不得要求Base Pass或Tonemap先执行

### Requirement: ImGui pipeline 保持跨后端等价
ImGui pipeline MUST使用公共 triangle list、无cull、无depth/stencil、RGBA write与确定的source-alpha blend语义，font texture和参数通过逻辑Pass binding提供。后端不得要求push constant、bindless或超出VulkanPortable v1四个physical sets的布局。

#### Scenario: D3D11 UI draw
- **WHEN** D3D11 FL11_0 backend实现ImGui renderer
- **THEN** 相同draw payload MUST能以vertex/index buffers、constant buffer、SRV/sampler、scissor与indexed draw完成，不得引入Vulkan专用分支
