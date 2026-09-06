## Purpose

定义跨 Vulkan、D3D11、D3D12 与 VulkanPortable v1 的统一 RHI device 根门面及对象创建契约，使上层获得单一入口，同时保持命令录制、提交和 presentation 的独立职责。

## ADDED Requirements

### Requirement: RHIDevice 是显式拥有的唯一后端根入口
Renderer MUST 显式拥有并向需要创建 RHI 对象的上层组件注入一个 `RHIDevice`。上层 SHALL 通过该对象查询 capabilities、limits 与 format support，创建 resource、view、shader、binding、pipeline、fence、viewport 和 device-level command context，并取得 graphics queue。公共 RHI MUST NOT 增加可变全局 device、静态 singleton、service locator 或与 `RHIDevice` 平行的 `RHISystem`/`RHIManager` 创建入口。

#### Scenario: Renderer 初始化 RHI
- **WHEN** Renderer 完成后端选择并创建 device
- **THEN** 后续 bootstrap、resource prepare 与 viewport 创建 MUST 使用显式传递的同一 `RHIDevice`

#### Scenario: 非 Renderer 模块请求 RHI 服务
- **WHEN** RenderScene 组件需要创建资源或 pipeline
- **THEN** 它 MUST 使用调用链传入的 `RHIDevice`，不得从全局或静态入口查找 device

### Requirement: 公共创建入口执行统一 frontend contract
每个公共 device 创建操作 MUST 在进入 backend native 创建前完成适用于该对象的 descriptor 结构验证、device lifecycle admission、初始化状态、capability/limits/format support 与 device ownership identity 检查。后端 MUST 只接收已通过公共检查的输入，不得成为公共 contract 的唯一执行者。

#### Scenario: 非法 descriptor
- **WHEN** 调用方提交结构无效的 resource、view、sampler、binding、pipeline、viewport 或 fence 创建输入
- **THEN** 公共 frontend MUST 返回 `InvalidArgument` 或对应的可诊断错误，且不得调用 backend native 创建

#### Scenario: 超出 device 能力
- **WHEN** descriptor 结构合法但请求的 format usage、sample count、slot 数量、alignment 或其他能力超过 device 支持
- **THEN** 公共 frontend MUST 返回 `Unsupported`，且不得依赖 backend 偶然失败来表达该结果

#### Scenario: 合法创建
- **WHEN** 输入通过公共 validation、lifecycle、capability 与 ownership 检查
- **THEN** frontend MUST 恰好一次调用对应 backend 创建路径并原样保留其成功值或可诊断失败

### Requirement: 全部 device 创建操作共享生命周期 admission
resource、view、shader、binding layout、binding set、sampler、graphics pipeline、GPU fence、viewport context 与 device-level graphics command context 创建 MUST 共享同一创建 admission。shutdown 开始后 MUST 拒绝新的创建；已经获得 admission 的创建 MUST 在 cache 和 backend device state 被释放前结束。

#### Scenario: Shutdown 与新创建竞争
- **WHEN** shutdown 已开始后另一线程请求创建 RHI 对象
- **THEN** 创建 MUST 返回 `NotReady` 或等价 lifecycle 错误，且不得进入 backend

#### Scenario: Shutdown 等待在途创建
- **WHEN** shutdown 开始时已有创建操作获得 admission 且尚未返回
- **THEN** shutdown MUST 等待这些创建结束，再清理 frontend cache 和 backend state

#### Scenario: Backend 创建失败
- **WHEN** 已获 admission 的 backend 创建返回失败
- **THEN** admission MUST 被释放，失败 MUST 不得永久阻塞 shutdown 或后续合法创建

### Requirement: 跨 device 对象组合在 native 调用前失败
凡创建输入引用既有 RHI object，公共 frontend MUST 验证这些对象均由当前 `RHIDevice` 创建；跨 device 的 resource/view、shader/layout、binding、pipeline attachment 或其他组合 MUST 在 backend native 调用前失败。validation MUST 使用公共 device identity，不得依赖 backend downcast。

#### Scenario: View 引用其他 device 的 resource
- **WHEN** device B 请求为 device A 创建的 buffer 或 texture 创建 view
- **THEN** frontend MUST 返回 `InvalidArgument`，且 device B backend MUST 不收到该请求

#### Scenario: Binding set 混用 device
- **WHEN** binding set descriptor 的 layout 或任一 resource/view/sampler 来自其他 device
- **THEN** 整个创建 MUST 失败，不得创建部分 native binding state

### Requirement: 创建结果保留统一 device identity
成功创建的 resource、view、shader、binding、pipeline、fence、viewport 派生对象与 command recording object MUST 可由公共层识别其 owner device。backend 返回与当前 device identity 不一致的对象 MUST 作为可诊断 backend contract failure 处理，不得发布给上层。

#### Scenario: Backend 返回错误 owner
- **WHEN** backend 创建成功结果携带的 owner identity 不是发起创建的 device
- **THEN** frontend MUST 返回 `BackendFailure` 或等价可诊断错误，并不得向调用方发布该对象

### Requirement: Device 门面不接管命令执行职责
`RHIDevice` SHALL 负责对象创建、能力查询和取得执行对象；draw-time transition、binding、render pass 与 draw MUST 由 `RHIGraphicsCommandContext` 执行，submit/completion MUST 由 `RHIQueue` 执行，acquire/frame closure/present MUST 由 `RHIViewportContext` 与 `RHIFrameContext` 执行。现有业务 pass MAY 在准备资源、pipeline 与 binding 时显式消费调用方提供的 `RHIDevice`，但 MUST 只通过调用方提供的 graphics context 录制 GPU 命令，并且不得通过 device 隐式创建 command list、submit、present 或 wait。本 change MUST NOT 为满足该边界而拆分现有 pass orchestration。

#### Scenario: Base Pass 录制
- **WHEN** Renderer 调用业务 pass 并传入当前 frame 的 graphics context
- **THEN** pass MUST 只在该 context 上录制 render pass、state 与 draw；它 MAY 使用显式传入的 device 创建 pipeline 或 binding，但不得调用 device 的 queue、viewport 或 context 创建入口

#### Scenario: Bootstrap 上传
- **WHEN** Renderer 在无 viewport frame 的 bootstrap 阶段上传 immutable resource
- **THEN** Renderer MAY 通过 device 创建独立 graphics context，但 MUST 显式 finish 并交给 graphics queue submit

### Requirement: 公共错误语义可由所有目标后端一致实现
统一 frontend contract MUST 可由 Vulkan、D3D12、D3D11 FL11_0 和 VulkanPortable v1 实现。结构错误、未初始化或关闭状态、不支持能力、device lost、内存不足与 backend failure MUST 保留可诊断分类；暂未实现的创建能力 MUST 返回 `Unsupported`，禁止无操作成功。

#### Scenario: 后端尚未实现某类创建
- **WHEN** 公共输入合法但选中 backend 尚不支持该对象或用法
- **THEN** 创建 MUST 返回 `Unsupported` 且不发布空壳对象

#### Scenario: Device lost 后创建
- **WHEN** device 已进入 lost/terminal 状态后收到创建请求
- **THEN** frontend MUST 拒绝进入普通 native 创建并保留 `DeviceLost` 或等价 terminal 诊断
