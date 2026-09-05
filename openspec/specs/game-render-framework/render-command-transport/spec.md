# game-render-framework/render-command-transport Specification

## Purpose
定义 Game Thread 到 logical Rendering Thread 的唯一 fire-and-forget 命令形状、FIFO、producer、inline fallback、ownership transfer、terminal disposal 和专用 façade 边界，避免把普通命令变成可恢复请求、额外 route/registry 或 RHI service locator。

## Requirements

### Requirement: 普通命令签名
普通 RenderCommand MUST 为 `void() noexcept` callable，支持 move-only capture，并由 Task Graph 直接保存具体 callable 类型。公共入口 MUST 具有等价于 `void enqueue_render_command(const char* command_name, Callable&& callable)` 的 fire-and-forget 形状；调用方必须传入 rvalue callable。

`command_name` MUST 非空且只用于诊断，不得成为 route、registry key、runtime type id 或执行策略选择。callable MUST NOT 接收由 transport 注入的 Renderer、RenderScene、RenderResourceManager、RHI context 或其他 service 参数。

#### Scenario: 独占 payload
- **WHEN** GT 将 unique ownership 移入 RenderCommand
- **THEN** payload MUST 只移动进入 Task Graph ownership，并在 logical RT 执行完成或 terminal skip 时析构

#### Scenario: 非法 callable
- **WHEN** callable不是move-constructible、不是rvalue或不能以`void() noexcept`调用
- **THEN** 该使用点 MUST 在编译期失败，不得退化为运行期type erasure或异常传播

### Requirement: 普通 enqueue 无返回值
`enqueue_render_command()` MUST 不返回 admission status、completion或业务结果。调用正常返回 SHALL 保证以下事实之一已经成立：callable已在logical RT同步执行并完成析构，或callable及其captured ownership已被Task Graph接受并将由logical RT执行/跳过和析构。

不存在“函数正常返回但命令未被接受”的可恢复分支。Renderer operation、RHI submit、GPU completion和present结果均不属于enqueue结果；它们 MUST通过`RendererStatus`、frame result、Fence/status provider或所属capability的错误通道传播。

#### Scenario: MultiThread normal return
- **WHEN** GT在transport admission开放时调用`enqueue_render_command()`且函数正常返回
- **THEN** callable ownership MUST已归Task Graph，调用方 MAY发布依赖该ownership transfer的opaque identity

#### Scenario: SingleThread normal return
- **WHEN** GT在SingleThread logical RT路径投递普通命令且函数正常返回
- **THEN** callable MUST已在同一logical RT contract内同步执行，调用方不得把该返回理解为GPU完成

#### Scenario: Draw内部失败
- **WHEN** accepted Draw在logical RT遇到Renderer或RHI错误
- **THEN** enqueue调用仍然没有返回值，错误 MUST由Renderer first-error/status路径发布而不是伪装为admission failure

### Requirement: Transport contract violation 不得正常返回
façade尚未建立或已经在producer-stop边界关闭、producer不是GT/logical RT、command name为空、Task Graph无法接受已声明可接受的任务或wrong-thread execution，均属于framework contract violation而不是普通可恢复业务分支。实现 MUST记录command name、producer、expected logical thread和原始framework error，并fail fast；不得静默丢弃callable后正常返回。

dispatch内部若在ownership transfer前失败，临时callable MUST在当前失败路径安全析构；若已进入Task Graph storage，则storage MUST负责安全回收。任何失败都不得通过另建fallback render queue继续执行。

#### Scenario: AnyWorker producer
- **WHEN** AnyWorker尝试投递普通RenderCommand
- **THEN** 框架 MUST记录producer与command name并fail fast，不得返回一个供调用方忽略的status

#### Scenario: Façade关闭后误投递
- **WHEN** Engine已经停止GT producers并关闭transport admission后仍调用enqueue
- **THEN** 该调用 MUST被视为lifecycle contract violation且不得正常返回

#### Scenario: Task Graph dispatch不变量失败
- **WHEN** transport处于accepting状态但Task Graph无法接收命令
- **THEN** 框架 MUST保留原始诊断并fail fast，不得让调用方在ownership未知时继续发布普通运行状态

### Requirement: 执行选择
GT+MultiThread SHALL enqueue；logical RT SHALL在调用点inline；GT+SingleThread SHALL通过相同logical RT contract inline；AnyWorker与Unknown/external producer第一阶段 MUST被拒绝。inline只改变调度位置，不得绕过thread validation、terminal execution permission、Renderer/Scene/resource contract或ownership disposal。

#### Scenario: Worker producer
- **WHEN** AnyWorker 尝试投递普通 RenderCommand
- **THEN** 框架 MUST fail fast 并记录 producer 线程与命令名

#### Scenario: Logical RT嵌套命令
- **WHEN** 一个正在执行的RenderCommand在logical RT投递另一条普通命令
- **THEN** 内层callable MUST在当前调用点inline执行，不得重新入队形成隐藏重排

#### Scenario: SingleThread执行
- **WHEN** GT与logical RT映射为同一GameThread
- **THEN** callable MAY同步执行，但其terminal gate、错误传播与析构语义 MUST和MultiThread路径一致

### Requirement: 唯一 FIFO transport
异步 RenderCommand MUST只使用Task Graph RenderingThread named queue；不得为Scene、Material、Resource或Draw建立第二条队列。同一GT producer被接受的Scene、Material、Resource与Draw命令 MUST按投递顺序执行或按terminal规则处置。`RenderCommandFence`使用同一named queue建立CPU到达顺序，但它属于tracked completion，不是可被terminal跳过的普通业务callable。

第一阶段只有GT可以作为普通异步producer，因此不承诺多个任意producer之间的全局FIFO；logical RT nested inline command按调用点发生，不伪装为排在已有queue尾部。

#### Scenario: 更新先于 Draw
- **WHEN** GT 依次投递资源更新和 Draw
- **THEN** RT MUST 在 Draw 前应用该更新

#### Scenario: Scene、Resource与Fence连续投递
- **WHEN** GT依次投递Scene update、Resource release、Draw与RenderCommandFence
- **THEN** logical RT MUST按接受顺序执行或处置前三项，并保证Fence在它们之后报告CPU到达

### Requirement: Terminal 分离执行许可与短暂 admission
Renderer进入terminal时 MUST立即关闭普通业务执行许可，使已经接受的普通callable按FIFO出队但跳过业务体，并在logical RT析构capture。为处理GT尚未观察到异步terminal的竞争窗口，transport admission MAY在composition root停止GT producers前短暂保持开放；该窗口中新接受的普通命令 MUST同样只进入skip/disposal路径，不能执行业务体。

GT通过`RendererStatus`或FrameEndSync观察terminal后 MUST先停止producer，再由RenderingThread关闭transport admission。admission关闭后不再允许普通投递。`RenderCommandFence`与terminal waiter MUST继续完成并唤醒GT，不得随普通业务体一起skip。

#### Scenario: Terminal发生在pending Draw之前
- **WHEN** Draw ownership已被接受但Renderer在执行前进入terminal
- **THEN** Draw业务体 MUST跳过，owned SceneRenderer payload MUST在logical RT析构，后续tracked Fence MUST完成并返回first error

#### Scenario: GT尚未观察到terminal
- **WHEN** Renderer刚关闭业务执行许可而GT有一条已开始投递的resource command
- **THEN** transport MAY接受其ownership并在logical RT skip/dispose，不得正常返回后静默泄漏或在GT析构RT-only payload

#### Scenario: Producer停止边界
- **WHEN** GT观察terminal并停止World/frame/resource producers
- **THEN** RenderingThread MUST关闭transport admission，之后才能完成accepted FIFO disposal和Renderer teardown

### Requirement: 窄 façade
框架 SHALL只暴露无返回值`enqueue_render_command()`及按领域定义的Scene/resource fire-and-forget操作，不得提供`get_renderer()`、`get_render_resource_manager()`、Task Graph getter或全局RHI command list。内部只允许一个active Task Graph transport publication：对同一instance的幂等publication MAY成功，尝试发布不同active instance MUST作为可诊断framework failure拒绝。

#### Scenario: 命令需要资源初始化
- **WHEN** GT 请求初始化 render resource
- **THEN** 调用方 MUST 使用资源 capability 的专用入口，而不是在 callable 中全局查找 manager

#### Scenario: Renderer尚未Running
- **WHEN** Renderer仍在Starting且普通transport尚未published
- **THEN** Game/Scene/resource producer MUST不调用enqueue；错误调用属于lifecycle contract violation，不得通过查询Renderer内部pointer绕过
