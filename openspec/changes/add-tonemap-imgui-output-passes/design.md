## Context

参见 `proposal.md` 的动机。当前 `ForwardSceneRenderer` 让 `SceneColor` 采用 swapchain format，在 Base Pass 后把它 copy 到 backbuffer，再开启一个 `Load/Store` 但不绘制内容的 UI pass。现有 RHI 已具备 texture/view、render pass、graphics pipeline、Pass binding、upload、viewport/scissor、`draw()`、`draw_indexed()`、资源 transition、command-list local state、submit publication 与 queue completion，因此第一阶段缺口在 renderscene 编排和资源契约，而不是新的公共 RHI 命令。

Renderer 仍遵守一个 viewport frame、一个 graphics context、一个 immutable business list 和一次 `end_frame()` 的模型。Game Thread（GT）可领先 logical Rendering Thread（RT），`FrameEndSync` 只证明 RT CPU 已处理到 fence，不证明 GPU completion；所有动态 UI buffer 与提交 payload 都必须据此处理生命周期。

Dear ImGui 当前依赖版本较旧，第三方 target 还编译并公开传播 GLFW/Vulkan backend。目标集成只把 ImGui 当作 immediate-mode UI、Docking、字体排版与 `ImDrawData` 生成库。Vulkan-Samples 可参考其 draw-data 解释方法，但其 native descriptor、command buffer、push constant、surface transform 和同步模型不进入 Toy3d 的 renderscene 或公共 RHI。

## Goals / Non-Goals

**Goals:**

- 建立 `HDR SceneColor -> Tonemap -> final color attachment -> optional ImGui` 的明确最终输出链，并删除正常帧中的 SceneColor copy。
- 把Tonemap和ImGui实现为两个独立RHI render pass；Runtime按顺序写同一backbuffer，未来Editor则可组合离屏Scene Viewport texture。
- 让 ImGui context、输入、draw snapshot 与 RHI renderer 具有明确的 GT/RT ownership、错误边界和 completion-aware 生命周期。
- 所有 shader、binding、pipeline 与 draw 只依赖公共 RHI，并可映射到 Vulkan、D3D11 FL11_0/SM5、D3D12 和 VulkanPortable v1。

**Non-Goals:**

- 不建立 RDG、通用 pass scheduler、后处理图或新的提交层。
- 不实现自动曝光、Bloom、color grading LUT、HDR10/scRGB、wide gamut 或完整 ACES color management；第一阶段的 filmic curve 只提供确定的 SDR 外观。
- 不实现 Dear ImGui multi-viewports、多原生窗口、任意 `Image()` texture registry、普通用户 render callback 或 Editor 面板。
- 不让 `ImGuiSystem` 成为 Runtime Input 的替代品，也不把 ImGui 类型扩散到 RHI 公共接口。

## Decisions

### 1. 最终输出采用 Runtime 与 Editor 两种拓扑

Runtime 主 viewport 使用直接输出路径：

```text
Base Pass             Tonemap pass               ImGui pass               Present
SceneColor HDR ──> [Discard / Store] ──> [Load / Store, optional] ──> transition
R16G16B16A16Float      fullscreen draw             alpha-blended UI
```

Tonemap对最终attachment执行覆盖式全屏draw，因此自己的color load/store为 `Discard/Store`。存在UI payload时，Tonemap pass先结束，ImGui再以 `Load/Store` 开启独立pass并alpha blend到同一backbuffer；不存在UI payload时完全跳过ImGui pass。两个pass仍录制到同一个graphics context和immutable business list，只执行一次submit。相较共享scope，这里接受一次中间Store/Load，以换取输入、binding、pipeline和pass ownership的清晰边界。

未来 Editor 使用组合路径：

```text
Scene scope       Scene final scope                  Editor final scope
Base Pass ──> Tonemap ──Store/transition──> LDR Scene View texture
                                                   │
Editor UI + Image(texture registry) <──sample──────┘ ──> Editor backbuffer
```

因此Tonemap的输入/输出由frame orchestration显式提供，既不获取swapchain，也不认识ImGui；`ImGuiRenderer`同样只认识调用方提供的目标attachment。这一组织与UE4.27常见的“Tonemap完成最终输出，Slate随后以Load/Store绘制UI”保持一致。备选的“永远Tonemap到中间LDR再copy”会增加额外纹理和copy；“让ImGui属于ForwardSceneRenderer”则会妨碍无World的Editor UI frame，均不采用。

### 2. SceneColor 固定为线性 HDR，第一阶段输出固定为 SDR sRGB-to-UNorm

`SceneRenderTargets` 按 viewport extent 创建 `R16G16B16A16Float`、`RenderTarget | ShaderResource` 的 SceneColor；resize/rebuild 继续沿既有 viewport/target 生命周期重建。Base Pass 不再以 presentation format 决定 SceneColor，也不在 shader 尾部做 SDR clamp。

第一阶段 `TonemapParameters` 以 exposure EV 表达手动曝光，默认 `0.0`，shader 先计算 `max(scene_rgb, 0) * exp2(exposure_ev)`，再使用固定的 ACES-inspired fitted curve：

```text
y = saturate((x * (2.51 * x + 0.03)) /
             (x * (2.43 * x + 0.59) + 0.14))
```

随后以标准分段 sRGB transfer function 编码到 `B8G8R8A8UNorm`，alpha 固定输出 `1.0`。这是确定、易跨后端验证的 filmic baseline，不宣称等价于 UE 完整 Tonemapper 或 ACES RRT/ODT。CPU 侧拒绝非有限 exposure；shader 对负颜色归零，并在曲线前对非有限/极端输入采用有界保护，避免 NaN/Inf 污染 attachment。备选的 Reinhard curve 过早去饱和高光，直接 linear-to-UNorm 又不能表达显示编码，均不采用。

`B8G8R8A8UNorm` 是非 sRGB storage，因而 Tonemap shader负责编码；ImGui packed vertex color也按 display-encoded SDR 值处理，并在该 attachment 上执行常见的 display-space alpha blend。第一阶段不同时引入 sRGB attachment，以免出现硬件 encode 与 shader encode 的双重转换。未来新增 sRGB/HDR target 时必须以新的 output profile 明确谁负责 transfer function，不能按 format 名称隐式猜测。

### 3. Tonemap 与 ImGui 各自拥有独立 pass scope

frame orchestration负责固定顺序和目标状态，但Tonemap与ImGui分别形成完整、可单独验证的pass。Runtime先把backbuffer从 `Present` 转为 `RenderTarget`，开启Tonemap `Discard/Store` pass，绑定SceneColor与Tonemap参数并完成全屏draw后结束；存在UI payload时，再开启ImGui `Load/Store` pass，绑定vertex/index/font及UI projection并完成draw后结束，最后把backbuffer转回 `Present`。两个pass之间backbuffer保持 `RenderTarget` access，不插入无意义的Present transition、copy、resolve或额外submit。

该选择刻意反映两阶段输入和pipeline状态不同：Tonemap读取HDR SceneColor并覆盖目标，ImGui读取动态geometry/font并混合已有目标。独立scope让load/store、错误位置、GPU marker和未来pass扩展更直观，也与Editor的离屏Scene Viewport路径一致。这里仍不新增通用scheduler；现有frame orchestration按明确顺序调用两个pass。

若UI payload为空，不创建ImGui pass。若只有Editor UI而没有Scene/Tonemap，则Editor orchestration可按自身clear/load策略开启独立ImGui pass。备选的共享scope能省去一次中间Store/Load，但会让两个具有不同输入契约的阶段共同依赖外部scope，并使Runtime与Editor形成不同的结构基线；在没有profiling证据前不采用。

### 4. ImGui context 与 GPU renderer 分属 GT/RT

composition root 在 GT 显式拥有 `ImGuiSystem`。一帧顺序为：平台事件更新 Runtime Input物理状态并同步给 ImGui IO，World/Game update，Application UI hook构建控件，`ImGui::Render()`，然后 `ImGuiSystem` 验证并深拷贝出 move-only `ImGuiDrawData`。payload 与本帧 `SceneRenderer` 一起转移给 Draw command；GT 随后可开始下一帧，RT 不回读 ImGui 内存。

Renderer domain 在 logical RT 拥有 `ImGuiRenderer` 和 `TonemapPassResources`。它们只接收稳定 payload、RHI resources 和调用方给出的 pass target。Application UI hook不获得 Renderer/RHI对象；Engine 也不新增 service getter。这样 Runtime debug UI 与未来 Editor Docking 共用同一生成/渲染边界，而不要求 Editor 复用 `ForwardSceneRenderer`。

不采用官方 `imgui_impl_*` renderer/platform backend：这些 backend 组合了 native device、descriptor 和窗口策略，会绕过现有 RHI、线程和生命周期 contract。平台层只补齐 Toy3d Input 所缺的文本、focus、DPI/framebuffer scale 事件语义。

### 5. draw snapshot 扁平化、先验证后发布

snapshot 遍历所有 `ImDrawList`，把顶点转换成第一方稳定的 `ImGuiVertex`，按原始 `ImDrawIdx` 宽度保存紧凑 index bytes，并记录 index stride；命令保存扁平数据中的 global list base，加上 ImGui 的 `IdxOffset`/`VtxOffset`。RT 由 index stride选择已有 RHI index format，并用显式 base/index offset录制 `draw_indexed()`。payload还保存 `DisplayPos`、`DisplaySize`、`FramebufferScale`，不保存第三方容器或指针。

发布前做一次全量验证：尺寸、scale、clip rect与offset必须有限；framebuffer extent乘法不得溢出；所有 vertex/index/command range必须落在 payload 内；element count和byte offset必须能由RHI参数表示；texture identity必须可解析；callback只能是 reset-state sentinel。任一命令非法即拒绝整个 UI payload并记录具体 draw-list/command 位置，不做部分 UI 绘制。场景和 Tonemap仍可在该帧继续，因为错误发生在 GT snapshot publication 前。

scissor 按 `(ClipRect - DisplayPos) * FramebufferScale` 计算，使用浮点交集裁到 `[0, framebuffer_extent]` 后再安全转换为整数；空交集跳过 draw。viewport覆盖 payload framebuffer extent。Y方向、front face和 surface transform继续由 backend既有 viewport convention处理，shader不写 Vulkan分支。

备选的直接传递 `ImDrawData*` 会在下一次 `NewFrame()` 后失效；在 RT 执行用户 callback会跨线程执行任意外部代码并破坏 immutable list假设，均不采用。

### 6. `ImTextureID` 只承载稳定整数 identity

`ImGuiTextureId` 是非零整数 identity；零表示 invalid，第一阶段保留一个稳定值表示 font atlas。GT 把该值编码进 ImGui 的 `ImTextureID`，snapshot只解码/验证数值，绝不解释为 RHI pointer、native descriptor或对象地址。RT `ImGuiRenderer` 将 font identity解析为自己持有的 texture view/sampler。

任意非 font identity使本帧 snapshot失败。未来 Editor 的 Scene Viewport `Image()` 需要单独增加 completion-aware texture registry：registry负责generation、注销与跨帧引用，而不是放宽本 change 的 pointer规则。这样本方案可用于 Editor 的控件、Docking和字体，但 Scene View嵌入要等 registry capability完成。

### 7. 字体 atlas 属于 all-or-nothing Renderer bootstrap

GT 创建 ImGui context、配置字体并从 ImGui core取得 RGBA32 atlas，立即复制成 immutable `ImGuiFontAtlasData`；完成后不把 atlas pointer交给 RT。composition root把font payload与预加载的 Tonemap/ImGui immutable ShaderMap Program refs一起作为 bootstrap输入。

RT 在 device-level graphics context中创建font texture/view/sampler、录制upload和 sampled transition；它尽量与 placeholder upload合并成一次 list，显式 submit并只等待该completion。全部 Tonemap shader resources、ImGui shader resources和font resources成功后才发布 Renderer Running。任何一步失败都按现有 first-error、逆序回滚和 terminal规则处理；普通帧不等待font upload，也不延迟创建font texture。

字体 DPI 策略第一阶段为单 atlas、由启动时主窗口 scale决定；运行中 DPI变化会立即更新布局/scissor scale，但不会在帧中隐式重建atlas。需要多 DPI 字体或动态重建时，后续通过显式replacement和completion retirement扩展。

### 8. Shader、binding 与 pipeline 都走现有 ShaderMap/RHI

Tonemap使用一个 fullscreen triangle的 built-in Global/Pass Program：vertex shader由 `SV_VertexID` 生成位置/UV，不需要vertex buffer；pixel shader通过 Pass group读取SceneColor view、sampler和小型参数buffer。ImGui Program使用 `ImGuiVertex` vertex layout，projection参数与font view/sampler也进入逻辑Pass binding。shader源进入 `engine/shader` 的既有构建输入，Renderer只消费经target/profile和reflection验证的 `ShaderMapEntry`/`ShaderCodeLibrary`，不读取裸binary。

`.shader` v1 增加顶层 `Parameters` section，并在第一阶段只允许 `Pass` group。它只接受 `Float`、`Float2`、`Float3`、`Float4` 和 `Float4x4` 数值类型，可选默认值省略时按全零处理；同组成员按源码顺序进入一个 ToyShaderABI constant buffer，并继续受 16 KiB 上限约束。`Resources` 仍只声明 texture、sampler 与 buffer descriptor，`Properties` 仍拥有 Material 参数和编辑器元数据，`View`/`Object` 仍由引擎固定 schema 提供。第一阶段不开放数组、struct、多个自定义 constant buffer、native `cbuffer`、`register` 或 descriptor set。这样 Tonemap 的 `exposure_ev` 与 ImGui 的 `projection` 共享稳定的参数编译路径，又不改变五组逻辑 Binding ABI。

Tonemap pipeline为triangle list、无cull、无depth/stencil、覆盖写；ImGui pipeline为triangle list、无cull、无depth/stencil、scissor开启、RGBA write。ImGui color blend使用 `SrcAlpha / OneMinusSrcAlpha`；alpha channel使用 `One / OneMinusSrcAlpha`，因此Tonemap写入alpha=1后UI仍保持最终alpha=1。reset-state sentinel重新绑定完整的ImGui pipeline、viewport、scissor前的公共状态、vertex/index buffers和Pass binding。

两个资源 owner 可按 color format、sample count及固定state从现有 device pipeline cache获取pipeline；不得缓存viewport/scissor等动态帧状态，也不得引入Material identity。VulkanPortable映射不超过既定四个physical descriptor sets；D3D11使用constant buffer、SRV/sampler、RTV和indexed draw；D3D12使用等价root binding与resource state。无需push constant、bindless、input attachment、compute或subpass。

### 9. 动态 UI buffer 使用 completion-aware page pool

`ImGuiRenderer` 在 RT持有vertex/index upload-capable buffer page池。每帧先从“从未提交”或completion已达到的page选择容量足够者；不足时按有上限的增长策略新建page，不覆盖in-flight内容，也不做逐帧wait。录制引用的RHI buffer与staging payload由immutable command list继续保活。

business submit成功后，当前page才关联返回的queue completion；recording failure、`abort_frame()`或明确submit failure按未提交路径归还，不发布虚假的in-flight marker。达到completion后page可复用；缩容或销毁也只能在安全completion后发生。为防止恶意或意外UI量导致无界分配，snapshot总顶点、索引、命令和单帧upload bytes设可配置硬上限，超限按局部UI错误拒绝payload。

备选的每帧创建/销毁buffer会增加驱动压力；单buffer环形覆盖需要更复杂的suballocation和同步；每帧wait会破坏现有并行模型，均不采用。

### 10. 输入先维护事实状态，再执行类别化 capture

平台窗口事件先进入既有 Input device状态；同一事件随后桥接到 GT ImGui IO。ImGui frame建立后，mouse、keyboard和text类别分别依据对应capture意图决定是否触发gameplay binding callback。release和focus-lost永远更新物理状态，即便业务callback被UI抑制，避免卡键。

`TextInputEvent` 只承载已验证Unicode scalar，不从 `KeyCode` 推导字符；IME组合由平台最终提交的文本事件表示，第一阶段不新增候选窗平台UI。`WindowFocusEvent` 在失焦时清Runtime Input与ImGui中的pressed/button/modifier瞬时状态。每帧从Window取得逻辑display size和实际framebuffer extent，计算有限正scale；零extent沿既有 viewport `NotReady` 跳帧。

交互式ImGui启用前，平台适配必须声明最低输入集合可用。缺文本或focus等能力时返回 `Unsupported`，也可由配置显式关闭ImGui；不提供“看起来可用但输入悄悄缺失”的模式。

### 11. 错误边界以 payload publication 和 submit truth 为界

GT snapshot validation失败属于局部UI错误：不发布payload，场景/Tonemap可继续。font、必需shader或pipeline bootstrap失败属于Renderer启动失败。RT开始business recording后，Tonemap或ImGui的任一RHI错误都使整个immutable list无效；frame orchestration调用 `abort_frame()`，丢弃command-list local resource state和未提交buffer使用，不能提交“只有Tonemap的一半成功帧”。

submit成功是资源access和buffer in-flight ownership的publication点。其后present `OutOfDate`只触发presentation rebuild，不回滚已提交事实；submit结果不确定或DeviceLost则沿既有terminal规则处理。关闭时先停止新UI payload，清队列中未执行payload，再在completion约束下释放动态buffer、font和pipeline refs，最后销毁viewport/device；GT ImGui context在RT teardown与thread join后销毁。

### 12. 第三方依赖升级只收敛 target，不承载引擎策略

依赖任务选择一个支持当前 event API、Docking和所需draw offsets的明确Dear ImGui稳定版本/commit，并把精确版本记录在第三方元数据中；版本选择是兼容性工作项，不改变上述contract。`imgui` CMake target只编译core与所需官方core组件，不编译 `imgui_impl_glfw`/`imgui_impl_vulkan`，不公开Vulkan include/library或GLFW依赖。`Toy3dRuntime`以 `PRIVATE` 实现依赖链接它。

除固定版本升级外不修改第三方core源码；所有Toy3d适配放入职责对应的 runtime input/renderscene 模块。若升级影响 `ImTextureID` 的配置表示，仍必须通过显式整数encode/decode适配，不能修改公共RHI或恢复pointer identity。

## Risks / Trade-offs

- [display-space UI blend不是线性光混合] → 第一阶段明确定位为SDR editor/debug UI，与常见ImGui/Slate观感一致；HDR或高精度合成以后新增独立output profile，不暗中改变现有结果。
- [固定 curve 与 UE4.27 完整Tonemapper外观不完全一致] → 使用黄金图/数值测试固定baseline，并把自动曝光、color grading和ACES color management留给独立后处理change。
- [独立pass增加一次backbuffer Store/Load边界] → 接受该可见成本以保持UE风格职责和Runtime/Editor结构一致；通过GPU profiling确认成本，只有形成独立change和跨后端证据后才考虑合并优化。
- [ImGui snapshot复制增加CPU和内存带宽] → 这是GT/RT解耦与一次性ownership的必要成本；扁平连续存储、move payload和page复用控制开销，后续用profile数据决定是否优化。
- [动态UI峰值造成buffer池膨胀] → 设置单帧硬上限、按completion回收，并在安全点裁撤长期闲置大page；不以wait idle换内存。
- [DPI变化后字体短期模糊或尺寸不理想] → 第一阶段保证坐标与裁剪正确并记录诊断；多atlas/异步atlas replacement作为后续能力。
- [第三方升级改变event、font或draw-data细节] → 固定精确版本，增加snapshot layout/offset/callback测试，并禁止第三方类型成为第一方长期接口。
- [D3D11没有显式resource barrier/render pass] → backend按公共access语义解除SRV/RTV冲突并保持load/store效果，上层不加入D3D11分支。
- [移动profile不支持HDR SceneColor组合usage] → 初始化/target创建阶段明确返回 `Unsupported`，不静默降级到8-bit或绕过Tonemap。
- [`Parameters` 与既有 engine schema 或 `Properties` 产生命名冲突] → frontend在统一generated-HLSL名称域中拒绝冲突；首阶段只开放Pass group，避免覆盖View/Object固定schema或形成第二套Material入口。

## Migration Plan

1. 先固定并收敛Dear ImGui core依赖，建立不链接官方backend的编译边界；补齐平台输入事件，但保持默认UI关闭，避免半完成路径进入普通帧。
2. 加入Tonemap/ImGui shader资产、ShaderMap身份和bootstrap资源验证；先用离屏/测试attachment验证curve、encoding、blend、vertex/index/clip语义。
3. 把SceneColor迁移为HDR format并实现Tonemap独立输出，在无ImGui时替换旧copy路径；验证resize、zero extent、OutOfDate、record/submit failure和三后端描述符可映射性。
4. 接入GT `ImGuiSystem`、输入桥、font bootstrap、draw snapshot和RT `ImGuiRenderer`，验证Tonemap `Discard/Store` 后以独立ImGui `Load/Store` pass保留并叠加backbuffer内容。
5. 删除空 `UIOverlayPass`、SceneColor-to-backbuffer copy及imgui官方backend target依赖；默认启用策略只在平台最低输入能力、shader和font bootstrap全部满足后开放。

每一步保持可独立构建和测试。合入前至少覆盖：Tonemap数值/黄金图、manual sRGB encode、空UI、16/32-bit index、非零offset、High-DPI clip、reset-state、未知texture/callback拒绝、buffer completion复用、frame abort、resize/minimize、bootstrap失败与terminal teardown。

回滚以完整feature开关恢复旧最终copy路径并禁用ImGui为短期集成手段，但新旧路径不得作为长期正式双轨。若HDR/Tonemap已成为后续pass依赖，则回滚整个change，而不是只恢复copy造成SceneColor format/encoding不匹配。第三方版本可回退到升级前锁定点；任何已提交GPU work仍按completion后释放，不能通过回滚绕过生命周期规则。

## Open Questions

- Dear ImGui 的精确稳定版本/commit在依赖任务中根据现有编译器、Docking与event API兼容性固定并记录；这不会改变本设计的core-only集成边界或任务结构。
- Runtime默认是否展示示例/metrics窗口属于产品配置选择；无论默认值如何，空UI payload和显式禁用路径都必须成立。
