# game-render-framework/renderer-terminal-shutdown Specification

## Purpose
定义 Renderer lifecycle、first terminal error、pending command disposal、正常 drain、DeviceLost 降级和 Engine/RenderingThread/Task Graph 的最终关闭顺序。

## Requirements

### Requirement: First-error latch
Renderer MUST以跨线程可观察的一致顺序原子发布terminal lifecycle并永久保留第一个原始terminal/framework error。`RendererStatus` snapshot MUST包含当前 `RendererLifecycleState` 和不可变primary error副本；secondary cleanup error只能作为附加诊断，不得覆盖primary code/message或伪装Renderer恢复。

#### Scenario: DeviceLost 后 wait failure
- **WHEN** DeviceLost 先发生且 shutdown wait 再失败
- **THEN** Game Thread 观察到的 primary error MUST 仍为 DeviceLost

#### Scenario: 两个线程同时报告 terminal
- **WHEN** presentation线程路径与waiter/cleanup路径竞争报告不同terminal error
- **THEN** 恰好一个first error MUST成为RendererStatus primary error，另一项只能成为secondary diagnostic

### Requirement: Renderer lifecycle 使用固定转换
正常Renderer lifecycle MUST遵守 `Stopped→Starting→Running→Stopping→Stopped`。Starting、Running或Stopping期间发生不可恢复framework/RHI错误时 MUST转入Terminal。同一个Renderer object一旦进入Terminal MUST保持sticky，不能重新发布Running；它只能执行有限teardown并最终销毁。

Starting只有在RHIDevice、RenderResourceManager、placeholder bootstrap/completion和primary viewport全部成功后才能转为Running。Stopping期间不得重新开放普通frame/resource façade。

#### Scenario: Bootstrap 成功
- **WHEN** Renderer从Stopped开始且全部RT bootstrap步骤成功
- **THEN** lifecycle MUST按Stopped→Starting→Running发布，普通frame/resource façade只能在Running后开放

#### Scenario: Bootstrap terminal failure
- **WHEN** Starting期间device或placeholder submission发生不可恢复错误
- **THEN** lifecycle MUST进入Terminal并执行有限回滚，不得尝试在同一Renderer object上重新Starting

#### Scenario: 正常停止
- **WHEN** Running Renderer收到正常shutdown且没有terminal error
- **THEN** lifecycle MUST按Running→Stopping→Stopped发布，teardown完成前不得提前发布Stopped

### Requirement: Terminal 先关闭业务执行并使 non-owning 状态安全
进入terminal MUST先锁存RendererStatus，立即停止新frame/resource init并关闭普通RenderCommand业务执行许可；已经接受的普通callable此后只能按FIFO skip/dispose，不得进入Renderer、RenderScene、RenderResourceManager或RHI业务体。Renderer随后 MUST abort current recording/acquired frame，并让RenderResourceManager discard当前recording、清除pending snapshot/pending/recording全部non-owning pointer；之后才能析构pending ownership payload。任何顺序都不得让manager或RenderScene在owner payload析构后回读dangling pointer。

业务执行许可关闭 MUST与transport ownership admission关闭分开。为覆盖GT尚未观察到异步terminal的竞争窗口，transport MAY继续短暂接受已经开始或紧邻发生的GT ownership payload，但这些payload MUST只进入logical RT skip/disposal路径。GT观察terminal后 MUST先停止World/frame/resource producers，再由RenderingThread关闭transport admission；此后普通enqueue属于lifecycle contract violation。

#### Scenario: Pending resource owner 被 skip
- **WHEN** pending command 持有 resource representation ownership
- **THEN** manager MUST 已停止解引用，payload MUST 在 logical RT 析构

#### Scenario: Active acquired frame
- **WHEN** terminal发生时viewport frame已经acquire但business list尚未安全submit
- **THEN** Renderer MUST先尝试abort闭合；abort边界未知或失败时viewport/device保持terminal且不得复用frame slot/image/synchronization

#### Scenario: Terminal 与 AddPrimitive 竞争
- **WHEN** Renderer刚关闭普通业务执行许可而GT有一条已经开始的AddPrimitive ownership投递
- **THEN** transport MAY接受Proxy payload，Add业务体 MUST不执行，Proxy MUST在logical RT析构；GT观察terminal后 MUST清空opaque render state并停止后续producer

#### Scenario: GT 观察 terminal
- **WHEN** FrameEndSync、RenderCommandFence或status snapshot把first error发布到GT
- **THEN** GT MUST停止新的World/frame/resource producer，然后关闭transport admission并进入有限terminal shutdown

### Requirement: Pending 与竞争窗口 command 确定处置
terminal前已经接受以及terminal后、producer-stop前竞争窗口内接受的RenderCommand MUST继续按FIFO出队；普通callable MUST被skip但其captured ownership payload MUST在logical RT disposal路径析构。RenderCommandFence/tracked completion callable MUST完成并向GT提供包含first error的RendererStatus观察结果，不能因terminal永久留在queue或让waiter挂起。

#### Scenario: Draw 与 Fence pending
- **WHEN** terminal 发生在 Draw 前
- **THEN** Draw SceneRenderer MUST 不执行但安全析构，后续 Fence MUST 唤醒 GT

#### Scenario: Proxy remove 与 resource release pending
- **WHEN** terminal发生时FIFO中仍有Proxy remove、MaterialRenderProxy/TextureResource release和Fence
- **THEN** callable业务体 MAY被skip，但所有ownership payload MUST在logical RT按出队顺序dispose，manager/scene non-owning状态 MUST已先安全化，Fence MUST最终唤醒GT

#### Scenario: Terminal 后竞争窗口仍有 ownership command
- **WHEN** GT尚未观察terminal且transport接受了一条新的MaterialRenderProxy或TextureResource ownership-transfer command
- **THEN** command业务体 MUST跳过，capture MUST在logical RT析构；transport不得静默丢弃后正常返回，也不得在GT析构RT-only payload

### Requirement: 正常 shutdown drain
正常shutdown MUST先停止普通Game/frame/resource producers，但保留composition root受控的shutdown投递窗口；随后让World执行 `unbind_scene()`、全部PrimitiveComponent destroy render state并投递Proxy/Material/TextureResource release与RenderCommandFence。全部shutdown command按确定顺序被transport接受后，RenderingThread MUST关闭transport admission；Fence drain后才能enqueue不经过普通producer façade、只供composition root使用的最终Renderer teardown task。等待RT CPU到达后才能request RenderingThread return、join RenderingThread、shutdown Task Graph，最后销毁Window/platform objects。正常路径不得以terminal skip替代应执行的remove/release。

#### Scenario: 正常退出有 pending uploads
- **WHEN** Engine 请求退出且资源仍 PendingUpload
- **THEN** RT teardown MUST discard pending recording、release representation/RHI refs并完成确定 shutdown

#### Scenario: World 仍绑定 Scene
- **WHEN** Engine请求正常退出而World仍有注册PrimitiveComponent
- **THEN** World MUST先unbind并将所有remove/release命令送入FIFO，Fence drain后Renderer final teardown才可清RenderScene

### Requirement: RT teardown 顺序
RT teardown MUST依次abort active frame、清 `PrimitiveSceneInfo`/Proxy和RenderScene、terminal/clear RenderResourceManager全部non-owning collections、释放Renderer-owned representations/refs、回收已完成queue work与deferred deletion、在device健康时执行有限queue idle/completion收敛、销毁viewport、placeholder和device，最后清 façade publication。实际in-flight RHI payload在确认completion前不得销毁。

#### Scenario: 正常 RHI teardown
- **WHEN** device 非 lost
- **THEN** queue wait/回收 MUST 在 native device 销毁前完成

#### Scenario: Scene 与 Resource 顺序
- **WHEN** final RT teardown开始
- **THEN** RenderScene中的PrimitiveSceneInfo/Proxy MUST先清空，Material/Texture/Mesh representations随后释放，viewport/placeholder/device最后销毁

### Requirement: DeviceLost 不无限阻塞
DeviceLost或状态未知同步失败后shutdown MUST不无限retry/wait idle。Renderer/backend MUST停止进一步不安全native调用，保留first DeviceLost error，释放CPU wrappers并走backend允许的有限terminal teardown；无法确认GPU idle时不得把native payload“已安全销毁”作为成功事实，但Engine exit和thread join仍必须有界完成。

#### Scenario: wait idle 返回 DeviceLost
- **WHEN** terminal teardown 无法确认 GPU idle
- **THEN** 系统 MUST 记录 secondary diagnostic并继续有限 teardown，不得挂死 Engine exit
