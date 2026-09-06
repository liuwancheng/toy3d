## 1. 依赖与构建边界

- [x] 1.1 盘点现有 ImGui 使用点和平台输入能力，选择支持 Docking、event API、`IdxOffset`/`VtxOffset` 的精确稳定版本或 commit并记录第三方版本，验证升级前后差异清单不包含官方 backend依赖需求
- [x] 1.2 升级固定的 Dear ImGui core文件且不修改其业务逻辑，验证第三方版本标识与选定commit一致并能以项目C++17配置独立编译
- [x] 1.3 收敛 `imgui` CMake target为core-only并移除Vulkan/GLFW公开传播，改为 `Toy3dRuntime` 的实现依赖，验证CMake配置成功且target link interface中不再出现backend、Vulkan或GLFW
- [x] 1.4 为本change涉及的新增第一方源码、shader输入和测试target登记CMake，验证源码均属于职责对应模块且Debug配置生成成功

## 2. ImGui GT 系统与输入边界

- [x] 2.1 实现 `TextInputEvent` 与Unicode scalar校验并接入现有Input事件通路，验证ASCII、非BMP字符、surrogate和越界code point测试通过
- [x] 2.2 实现 `WindowFocusEvent` 及失焦时Keyboard/Mouse/modifier瞬时状态清理，验证按键或按钮按住后失焦不会继续产生Hold且重新聚焦不恢复旧状态
- [x] 2.3 在现有平台适配中补齐主窗口鼠标、滚轮、键盘、modifier、文本、focus与DPI/framebuffer size投递，验证每个平台声明的最低交互能力可被初始化检查并对缺失能力返回 `Unsupported`
- [x] 2.4 实现类别化ImGui capture policy，使设备事实状态先更新、gameplay callback再按mouse/keyboard/text分别抑制，验证UI只捕获鼠标时keyboard mapping仍工作且被捕获按键release不会卡键
- [x] 2.5 实现GT-only `ImGuiSystem` 的显式context创建、frame开始/结束、Application UI hook与销毁，验证空hook产生合法空UI状态且hook无法访问Renderer/RHI服务
- [x] 2.6 实现逻辑display size、framebuffer scale与零extent处理，验证DPI变化更新下一帧payload且最小化时不生成可提交UI payload

## 3. ImGui snapshot 与纹理身份

- [x] 3.1 实现整数 `ImGuiTextureId` 的invalid/font atlas编码和 `ImTextureID` 安全适配，验证往返不依赖对象地址、指针宽度或backend descriptor
- [x] 3.2 实现 `ImGuiVertex`、`ImGuiDrawCommand` 与move-only `ImGuiDrawData` 的扁平深拷贝，验证GT开始下一次 `NewFrame()` 后frame N payload内容仍保持不变
- [ ] 3.3 支持16/32-bit index、global list base、`IdxOffset`与`VtxOffset`的snapshot表示，验证超过64K顶点和非零offset样例生成正确的RHI draw参数
- [ ] 3.4 实现snapshot全量range、有限数、extent乘法和单帧容量上限校验，验证越界command、NaN clip、整数溢出和超限payload均被整帧UI拒绝且不留下半构造ownership
- [x] 3.5 仅接受font texture identity与 `ImDrawCallback_ResetRenderState`，拒绝未知texture和普通callback，验证局部UI诊断包含draw-list/command位置且场景帧仍可无UI继续
- [x] 3.6 为snapshot生命周期、layout和第三方版本适配增加单元测试，验证 `ImGuiVertex` 字段映射、packed color、display metadata及payload析构路径全部覆盖

## 4. Tonemap 与 ImGui shader 资产

- [x] 4.0a 扩展 `.shader` v1 frontend/AST以解析仅支持Pass group的 `Parameters` 数值声明，验证重复section、非法group/type/default和跨section命名冲突均产生精确诊断
- [x] 4.0b 将Pass parameters接入ToyShaderABI layout、默认值、稳定hash、generated HLSL与reflection parity，验证成员源码顺序、全零省略默认、16 KiB上限和VulkanPortable set 1映射
- [x] 4.0c 更新Active Shader contract与EBNF，明确Parameters/Resources/Properties边界及首阶段延期能力，并构建运行Shader frontend/layout/compile测试
- [ ] 4.1 新增fullscreen triangle Tonemap Global/Pass shader及ShaderMap构建登记，验证所有目标reflection满足既有logical binding group规则且VulkanPortable不超过四个physical sets
- [ ] 4.2 实现手动exposure、固定filmic curve、非有限保护、分段sRGB encode与alpha=1，验证关键输入数值、黑白点、高亮单调性和跨target容差测试通过
- [ ] 4.3 新增ImGui vertex/pixel shader及ShaderMap构建登记，验证position/UV/packed color、projection和font采样reflection可映射到Vulkan、D3D11 SM5与D3D12
- [ ] 4.4 为Tonemap与ImGui Program identity、target/profile和binding不匹配增加启动前诊断，验证错误在native pipeline/draw前返回且保留原始ShaderMap上下文

## 5. Renderer bootstrap 资源

- [x] 5.1 实现 `ImGuiFontAtlasData` 的GT RGBA32复制与pitch/extent校验，验证RT输入不引用ImGui atlas内存且非法或空atlas使启用ImGui的bootstrap失败
- [x] 5.2 实现RT-only `TonemapPassResources`，创建并持有shader、binding layout、sampler和可复用pipeline引用，验证任一必需资源失败时Renderer不发布Running
- [ ] 5.3 实现RT-only `ImGuiRenderer` bootstrap资源，创建font texture/view/sampler、shader/binding/pipeline与初始buffer-pool状态，验证font identity仅在全部资源ready后可解析
- [ ] 5.4 把placeholder与font upload合并到显式device-level graphics list并等待指定completion，验证只发生一次可合并bootstrap submit/wait且普通帧不复用该同步路径
- [ ] 5.5 扩展all-or-nothing bootstrap回滚顺序和禁用ImGui路径，验证create、record、finish、submit、completion wait各注入失败点均不发布部分资源，禁用时Tonemap仍可启动

## 6. HDR SceneColor 与 Tonemap Pass

- [x] 6.1 将 `SceneRenderTargets` 的SceneColor改为 `R16G16B16A16Float` 和 `RenderTarget | ShaderResource`，验证创建描述符不再依赖swapchain format且不支持组合usage时返回 `Unsupported`
- [ ] 6.2 调整Forward Base Pass保持有限HDR输出并移除Tonemap前SDR clamp假设，验证大于1.0的测试颜色能保存在SceneColor中且depth/reversed-Z行为不变
- [ ] 6.3 实现Tonemap向调用方提供的attachment、extent和view rect录制fullscreen draw，验证既可写Runtime backbuffer也可写独立LDR texture且不读取swapchain/ImGui状态
- [x] 6.4 实现SceneColor `RenderTarget -> ShaderResourceGraphics`、输出目标到/离 `RenderTarget` 的状态链，验证只有business submit成功才发布final access，abort与明确submit failure均丢弃local state
- [ ] 6.5 删除正常帧SceneColor-to-backbuffer copy依赖并增加Tonemap输出图像/数值测试，验证旧copy不再录制、SDR backbuffer得到确定sRGB编码且resize后资源重建正确

## 7. ImGui RHI 绘制

- [ ] 7.1 实现completion-aware vertex/index buffer page选择、增长和回收，验证in-flight page不会被覆盖、容量增长不触发wait idle且已完成page可复用
- [ ] 7.2 使用当前frame context的显式upload路径上传UI数据并保活command-list payload，验证record abort/submit failure按未提交路径归还page而submit成功后关联真实completion
- [ ] 7.3 实现ImGui公共RHI pipeline state、Pass binding、viewport和indexed draw录制，验证blend为color `SrcAlpha/OneMinusSrcAlpha`、alpha `One/OneMinusSrcAlpha`且无depth/cull/backend分支
- [ ] 7.4 实现 `(ClipRect - DisplayPos) * FramebufferScale` 的安全scissor裁剪，验证High-DPI、负坐标、超出framebuffer和空clip在Vulkan/D3D语义下得到等价结果
- [ ] 7.5 实现font identity解析与reset-state sentinel的完整重新绑定，验证reset后pipeline、viewport、buffers和bindings均恢复且后续draw正确
- [ ] 7.6 支持ImGui向任意合法最终attachment独立录制，验证无SceneRenderer的UI-only测试frame可按调用方clear/load策略输出且不要求Tonemap资源

## 8. 最终输出编排与提交失败语义

- [ ] 8.1 在frame orchestration中固定Tonemap pass结束后再执行可选ImGui pass，验证二者分别begin/end独立RHI render pass但仍使用同一graphics context、同一immutable list和一次business submit
- [ ] 8.2 实现Runtime Tonemap `Discard/Store` pass与后续ImGui `Load/Store` pass，验证UI正确保留Tonemap结果并alpha blend，两个pass之间backbuffer保持 `RenderTarget` access且无额外copy、transition或submit
- [ ] 8.3 实现空UI与Editor离屏路径，验证空payload不创建ImGui pass；Tonemap输出离屏texture时执行Store/sampled transition且Editor UI使用自己的合法attachment pass
- [ ] 8.4 将一次性UI payload纳入现有Draw ownership和同一graphics list顺序，验证GT可立即构建下一帧且RT按FIFO在Base Pass、Tonemap后执行UI
- [ ] 8.5 统一Tonemap/ImGui recording failure处理，验证Tonemap已录制后UI失败会丢弃整个list、调用 `abort_frame()` 且不发布资源access或buffer completion
- [x] 8.6 验证submit成功后present `OutOfDate`保留已提交状态并只触发presentation rebuild，submit不确定或DeviceLost则进入既有terminal流程

## 9. 生命周期、清理与集成验证

- [ ] 9.1 扩展Engine composition root和主循环初始化/关闭顺序，验证GT ImGui context在RT teardown与RenderingThread join后销毁且Engine未新增Renderer/RHI service getter
- [ ] 9.2 扩展terminal disposal清理未执行UI payload、buffer recording状态与Tonemap/ImGui资源，验证pending Draw被skip时只析构owned payload、不调用ImGui/RHI且in-flight资源等待合法completion
- [x] 9.3 删除空 `UIOverlayPass` 和正式target中的官方ImGui backend调用/链接，验证仓库搜索仅在第三方源码或非构建参考中出现 `imgui_impl_vulkan/dx11/dx12/glfw`
- [ ] 9.4 增加Runtime端到端测试，覆盖空UI、基础控件/字体、Docking、16/32-bit index、DPI、resize/minimize、capture、未知texture/callback和buffer增长，验证所有场景输出或诊断符合delta specs
- [ ] 9.5 增加final-output失败矩阵测试，覆盖HDR format不支持、shader/font bootstrap、record、finish、submit、completion wait、present OutOfDate与DeviceLost，验证first-error、rollback和terminal状态符合现有contract
- [x] 9.6 配置并构建 `Toy3dEditor` 与受影响测试target，运行 `ctest --test-dir build -C Debug --output-on-failure`，验证Debug构建和全部登记测试通过
- [ ] 9.7 由独立sub-agent按 `verify-toy3d-build` 复核CMake、受影响目标、测试日志与关键源码边界，验证其报告无未解释失败且主agent已处理所有发现
- [ ] 9.8 更新 `document/index.md` 指向的Active渲染/Input设计与第三方版本说明，记录Runtime/Editor拓扑、颜色编码、ownership和已延期能力，验证长期contract未混入施工流水账且与OpenSpec最终实现一致
