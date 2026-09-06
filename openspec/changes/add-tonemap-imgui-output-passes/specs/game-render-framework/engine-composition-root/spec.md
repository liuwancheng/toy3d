## MODIFIED Requirements

### Requirement: Engine 拥有进程级框架对象
现有 `toy3d::Engine` SHALL 在 Game Thread 显式拥有 Platform、Window/RHISurface、Input、GT ImGui context owner、ShaderMap loader/cache所需process对象、Task Graph active instance、Renderer 稳定外壳、RenderingThread lifecycle controller 与 FrameEndSync，并按依赖逆序销毁。Engine MAY把预加载的immutable builtin ShaderMap Program refs和font atlas CPU payload作为Renderer bootstrap输入，但 MUST不向业务暴露Renderer/RHI service getter。Renderer 稳定外壳 MAY 由 Engine 持有，但其 RT 可变内部对象 MUST 只在 logical Rendering Thread 创建、访问和销毁。

#### Scenario: Engine 初始化
- **WHEN** Engine 开始完整 runtime 初始化且配置启用ImGui
- **THEN** 它 MUST 依次完成日志/文件系统/配置、Platform、Window/RHISurface与Input、GT ImGui context/font atlas、必需builtin Program加载、Task Graph与GameThread attach、Renderer稳定外壳、RenderingThread start和Renderer启动引导，全部成功后才进入主循环

#### Scenario: Renderer shell 已创建但 RT 尚未 ready
- **WHEN** Engine 已持有 Renderer 稳定外壳但 RenderingThread 尚未完成 attach 和启动引导
- **THEN** 普通 Scene/resource/frame façade MUST 保持关闭，Engine 不得通过稳定外壳读取或修改尚未发布的 RT 内部状态；GT ImGui context也不得访问未发布的font RHI resource

### Requirement: Game loop 只负责跨域编排
主循环 SHALL 按 Window event processing、Input/ImGui event routing、Game/World update、当前UI frame构建与immutable draw payload生成、本帧一次性渲染输入构造、Draw ownership投递和 `FrameEndSync::sync_frame()` 的顺序协调一帧。Game/World/UI mutable objects MUST 留在 GT；投递给 logical RT 的对象 MUST 遵守各自的 copied-value、owned payload 或显式 non-owning lifetime contract。

#### Scenario: 正常帧
- **WHEN** Window未请求关闭且Renderer为Running
- **THEN** Engine MUST先处理输入并完成GT update和UI frame，再投递本帧SceneRenderer与UI draw payload，最后执行FrameEndSync；FrameEndSync只同步RT CPU到达，不得被当作GPU completion

#### Scenario: UI snapshot validation 失败
- **WHEN** ImGui draw data包含不支持callback、未知texture identity或越界command而无法生成合法payload
- **THEN** Engine MUST记录UI诊断并允许本帧以无UI payload继续投递场景/Tonemap，不得让GT持有半构造payload或把该局部错误伪装成Renderer terminal

#### Scenario: Renderer terminal
- **WHEN** FrameEndSync 或其他只读状态观察到 Renderer terminal
- **THEN** Engine MUST 停止产生新的普通 render work和UI draw payload并进入有限关闭流程，不得尝试通过 Engine 重启同一个 Renderer object

## ADDED Requirements

### Requirement: Application UI hook 不暴露 Renderer 服务
Runtime/Application与未来Editor MAY在GT UI frame窗口内调用ImGui core构建控件，但hook MUST不接收RHIDevice、RHICommandContext、RenderScene、ImGuiRenderer或backend对象。无Application或hook不产生控件时 MUST生成合法空UI状态。

#### Scenario: Application 构建调试窗口
- **WHEN** Application在UI hook中提交ImGui控件
- **THEN** 它 MUST只修改当前GT ImGui context，最终GPU工作由后续draw payload和RT ImGui renderer完成
