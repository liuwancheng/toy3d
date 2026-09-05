# game-render-framework/frame-synchronization Specification

## Purpose
定义 Rendering Thread CPU 到达、Game/Render frame lag、显式 CPU flush 和 terminal 只读传播，使普通帧不会把 tracked Task Graph completion 与 GPU completion、RHI submit或present混为一谈。

## Requirements

### Requirement: Fence 只表示 RT CPU 到达
Fence task MUST作为tracked completion进入同一个RenderingThread named queue，并排在该GT producer先前已接受的普通RenderCommand之后。Fence完成 MUST只表示其前序RenderCommand callable已在logical RT执行或按terminal规则skip/dispose，不得表示GPU submit、GPU completion、present或deferred deletion回收完成。

Fence task不是普通业务callable：Renderer terminal时普通业务体 MAY被skip，但Fence task MUST仍执行、发布completion并唤醒waiter。Fence不得要求Renderer Running才能完成，也不得访问已经清理的RenderScene或RHI。

#### Scenario: GPU 尚未完成
- **WHEN** Draw 已 submit 且 RT 到达 Fence，但 queue completion 未达到
- **THEN** Fence MAY 完成，RHI in-flight payload MUST 继续保活

#### Scenario: Terminal 跳过前序 Draw
- **WHEN** Renderer在pending Draw之前进入terminal
- **THEN** Draw业务体 MUST按terminal规则skip、owned payload MUST先在logical RT disposal，随后Fence task MUST完成并唤醒GT

### Requirement: Fence begin 是显式同步操作
`RenderCommandFence::begin_fence()` MUST只允许GT在transport仍接受tracked completion时调用，并 MUST显式报告wrong caller、transport未建立/已关闭、同一Fence仍有未完成completion或Task Graph dispatch failure。该返回值不改变普通RenderCommand的无返回值contract。

#### Scenario: Begin 成功
- **WHEN** GT在有效transport生命周期内begin一个没有outstanding completion的Fence
- **THEN** begin MUST返回成功并保存足以完成后续wait的Task Graph/completion引用

#### Scenario: Begin 失败
- **WHEN** Task Graph无法建立tracked Fence completion
- **THEN** 调用 MUST返回原始framework failure，FrameEndSync/flush不得伪装RT已经到达

#### Scenario: 普通 enqueue 与 Fence 区别
- **WHEN** 调用方投递普通Material update后建立Fence
- **THEN** Material update调用没有admission返回值，Fence begin MAY返回显式同步建立状态，二者不得合并成一种request/result API

### Requirement: Fence 重用与生命周期明确
新构造且尚未begin的Fence SHALL视为complete。一个Fence有未完成completion时 MUST拒绝再次begin；前一次completion完成后 MAY复用并替换旧completion ref。`wait()` MUST只在GT调用，并且不得延长Task Graph active instance生命周期；composition root MUST保证begin到wait以及全部Engine-owned Fence销毁都早于Task Graph shutdown。

#### Scenario: 尚未 begin 的 Fence
- **WHEN** 首帧one-frame-lag等待轮转数组中尚未begin的另一个Fence
- **THEN** wait MUST立即视为RT reached，但仍 MAY通过只读status provider观察已经锁存的Renderer terminal/framework failure

#### Scenario: Outstanding Fence 重复 begin
- **WHEN** 同一Fence的前一tracked completion仍未完成
- **THEN** 新begin MUST返回InvalidState类framework failure且不得覆盖原completion

#### Scenario: Task Graph 生命周期
- **WHEN** Engine开始shutdown Task Graph
- **THEN** 全部FrameEndSync/RenderCommandFence wait与Renderer teardown同步 MUST已经完成，不得保留指向已销毁Task Graph的non-owning pointer

### Requirement: 默认最多领先一帧
FrameEndSync SHALL使用两个轮转Fence。默认one-frame-lag模式在frame N完成普通命令投递后begin `Fence[N % 2]`，轮转后等待前一Fence；首帧等待尚未begin的Fence而直接通过，从第二帧起GT最多领先logical RT一帧。zero-lag模式 MUST begin并等待同一个当前Fence。

#### Scenario: 默认 FrameEndSync
- **WHEN** GT 完成 frame N 的 Draw 和 Fence 投递
- **THEN** GT MUST 在 frame N-1 Fence 完成后才结束同步边界

#### Scenario: Zero-lag FrameEndSync
- **WHEN** 配置禁止Game/Render thread lag
- **THEN** frame N MUST等待frame N刚建立的Fence，但即使wait成功GPU仍 MAY继续执行该帧已submit work

#### Scenario: Begin 失败不轮转
- **WHEN** 当前frame无法建立Fence
- **THEN** FrameEndSync MUST返回framework failure且不得把轮转index推进到一个未建立的同步点

### Requirement: 显式 flush
`flush_rendering_commands()` SHALL创建局部RenderCommandFence、begin并等待logical RT CPU到达。它 MUST NOT隐式record/submit pending uploads、调用`end_frame()`、present、等待GPU completion/queue idle或回收全部deferred deletion。只有Renderer启动引导、loading、tool、test或shutdown等明确边界可通过所属capability额外要求submit pending work或等待指定GPU completion。

#### Scenario: 普通 Gameplay setter
- **WHEN** Gameplay 修改普通材质参数
- **THEN** setter MUST NOT 隐式 flush 或等待 GPU

#### Scenario: Flush 后 GPU 仍 busy
- **WHEN** flush前的Draw已经submit但queue completion尚未达到
- **THEN** flush MAY成功返回RT reached，RHI completion保活与资源回收 MUST继续独立工作

### Requirement: 普通 Game/Render 操作不隐式同步
Material setter、Texture update、Mesh resource init、Primitive transform update、`create_render_state()`和普通Draw enqueue MUST保持fire-and-forget，不得隐式begin/wait RenderCommandFence、调用flush或等待GPU。

#### Scenario: 连续 Primitive transform
- **WHEN** GT连续发送多个transform update后投递Draw
- **THEN** 顺序 MUST由同一FIFO保证，不得为每个update插入Fence或flush

### Requirement: Terminal 只读传播
Fence/FrameEndSync wait MUST通过只读status provider观察first terminal error或framework failure，但不得提供Renderer、RenderScene、resource manager或RHI的可变访问。`RenderFenceWaitResult`的RT reached、framework failure与renderer terminal结果 MUST互斥；Task Graph wait失败不得伪装为terminal，Renderer first error也不得被后续cleanup diagnostic覆盖。

status provider抛出异常 MUST转换为framework failure。wait实现 MUST确保terminal时tracked Fence仍可完成，或由terminal completion路径确定唤醒，不能因普通业务callable被skip而永久阻塞。

#### Scenario: 异步 terminal
- **WHEN** GT 正在等待 Fence 且 RT 锁存 terminal
- **THEN** waiter MUST 被唤醒并获得原始 terminal 结果

#### Scenario: GT 观察 terminal 后停止 producer
- **WHEN** FrameEndSync返回renderer terminal
- **THEN** Engine MUST停止新的World/frame/resource producer，进入transport producer-stop/admission-close流程，而不得尝试把Fence当作Renderer恢复入口

#### Scenario: Status provider失败
- **WHEN** Fence completion已到达但status provider抛出异常
- **THEN** wait MUST返回framework failure并保留诊断，不得报告普通RT reached
