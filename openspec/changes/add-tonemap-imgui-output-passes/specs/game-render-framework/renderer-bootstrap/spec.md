## MODIFIED Requirements

### Requirement: Bootstrap 顺序固定
RT启动引导 MUST按RHIDevice、RenderResourceManager、必需placeholder/Tonemap/ImGui RHI resource的device submission、exact completion wait、primary viewport的顺序执行，全部成功后才发布Renderer Running。所有可合并的bootstrap uploads SHOULD进入一次device-level graphics list和一次指定completion wait；任一步失败不得发布部分最终输出或UI资源。Renderer MUST先从Stopped发布Starting；成功后发布Running并开放普通Scene/resource/frame façade，失败则发布Terminal/RendererStatus first error并保持façade关闭。

#### Scenario: 正常启动
- **WHEN** Window/Surface、Task Graph、RenderingThread、immutable builtin Program refs与启用ImGui时的font atlas CPU payload ready
- **THEN** bootstrap MUST完成placeholder、Tonemap、font GPU可用性和viewport创建后才开放frame/resource façade

#### Scenario: 普通命令过早到达
- **WHEN** RenderingThread已经attach但Renderer仍处于Starting且启动结果尚未发布
- **THEN** 普通Scene/resource/frame façade MUST拒绝enqueue或保持关闭，不得让命令观察部分初始化domain

### Requirement: 启动引导输入与线程固定
composition root SHALL在GT创建Window、backend-independent RHISurface、GT ImGui context、font atlas immutable CPU payload并加载所需immutable ShaderMap Programs，再启动Task Graph/RenderingThread ready handshake；RHIDevice、Manager、placeholder、Tonemap/ImGui RHI resources和viewport的创建及可变状态 MUST只在logical RT执行。bootstrap result必须在普通façade开放前发布给composition root，GT不得轮询RT内部对象判断ready，也不得让RT读取GT ImGui context或font atlas内部pointer。

#### Scenario: Surface 与UI bootstrap输入已创建
- **WHEN** logical RT开始Renderer Starting流程
- **THEN** 它 MAY消费composition root提供的RHISurface identity、immutable Program refs和拥有像素副本的font payload创建device/resources，但不得读取Window、Application或ImGui context的可变internals

#### Scenario: Surface 已创建
- **WHEN** logical RT开始Renderer Starting流程
- **THEN** 它 MAY消费composition root提供的RHISurface identity创建device/viewport，但不得读取Window的可变platform/backend internals

### Requirement: Placeholder 全有或全无
placeholder、Tonemap与启用ImGui时的font resource create，upload/transition recording、finish、submit或completion wait任一步失败 MUST导致整个Renderer启动引导失败；不得发布部分placeholder/final-output/UI set。所有未发布refs MUST在RT按逆序释放，已进入command list的RHI/staging payload按submit truth与completion规则保活。Tonemap或ImGui shader Program identity、target/profile或binding不匹配 MUST在native pipeline/draw前失败。

#### Scenario: Bootstrap submit 失败
- **WHEN** 包含placeholder或font upload的device-level command list未成功submit
- **THEN** bootstrap MUST保留原始错误并释放未发布refs，Renderer MUST不进入Running

#### Scenario: Bootstrap completion wait 失败
- **WHEN** bootstrap list已成功submit但等待指定completion返回DeviceLost或不可恢复错误
- **THEN** Renderer MUST保留该first error、进入Terminal并执行有限teardown，不得把placeholder、Tonemap或ImGui资源发布为GPU可用或继续创建viewport

#### Scenario: Placeholder submit 失败
- **WHEN** device-level command list 未成功 submit
- **THEN** bootstrap MUST 保留原始错误并释放未发布 refs，Renderer MUST 不进入 Running

#### Scenario: Placeholder completion wait 失败
- **WHEN** placeholder list已成功submit但等待指定completion返回DeviceLost或不可恢复错误
- **THEN** Renderer MUST保留该first error、进入Terminal并执行有限teardown，不得把placeholder发布为GPU可用或继续创建viewport

#### Scenario: ImGui 被配置禁用
- **WHEN**配置明确禁用ImGui
- **THEN** bootstrap MUST不要求font payload或创建ImGui RHI resources，但Tonemap和正常场景输出仍必须完整可用
