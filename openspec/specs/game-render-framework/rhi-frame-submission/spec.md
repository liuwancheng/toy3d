# game-render-framework/rhi-frame-submission Specification

## Purpose
定义 viewport begin/end/abort、业务 command list submit 与 presentation 的跨后端结果语义，使上层能准确决定资源 commit、重试、resize 与 terminal。

## Requirements

### Requirement: 单 viewport 单录制单元
第一阶段每个 viewport Draw SHALL `begin_frame()`、创建一个 graphics context，并把 pending uploads、`init_views()`、visibility、测试构建中的简单 pass 与 Forward Base Pass 按显式顺序串行录制为一个 immutable command list；`end_frame()` 接口 MUST 保留未来接收多个 lists 的 vector 形状，但第一阶段不得隐藏第二个业务 list或独立submit。

#### Scenario: Upload 后 Draw
- **WHEN** 本帧 Mesh buffer upload 后立即被 Draw 使用
- **THEN** upload、transition 和 Draw MAY 在同一 list 中，CPU 不等待 GPU

#### Scenario: 测试 pass 与 Base Pass
- **WHEN** 测试构建启用简单 render-side pass
- **THEN** pending uploads、init_views、visibility、测试 pass与Forward Base Pass MUST串行录入同一graphics context/list并由一次end_frame提交

### Requirement: RHIFrameContext 只能被闭合一次
成功 `begin_frame()` 返回的 unique `RHIFrameContext` MUST 只在对应 `RHIViewportContext` 的当前 acquired frame中有效，并且必须恰好由一次 `end_frame()` 或 `abort_frame()` 消费ownership。跨viewport、跨device、重复闭合、过期frame或与非本frame command list组合 MUST在任何native call前失败。

#### Scenario: 重复使用 FrameContext
- **WHEN** 已被end_frame或abort_frame消费的FrameContext identity再次提交
- **THEN** 公共/backend validation MUST拒绝且不得复用frame slot、acquire synchronization或presentation image

#### Scenario: Command list 来自另一 device
- **WHEN** end_frame收到与viewport device ownership identity不一致的command list
- **THEN** end_frame MUST在native submit前失败并安全abort当前acquired frame

### Requirement: Submit 与 present 分离
`end_frame()` MUST返回 `RHIResult<RHIFrameEndResult>`。外层成功 MUST表示业务 lists确定已成功submit并返回有效completion；`RHIFrameEndResult::presentation_status` MUST独立表达Success、Suboptimal、OutOfDate或terminal。外层失败 MUST表示业务 lists确定没有成功submit且不得提供可用于业务commit的completion；若backend无法确定submit是否已经产生GPU工作，viewport/device MUST锁存terminal而不能返回可继续运行的普通失败。

#### Scenario: Submit 成功 present OutOfDate
- **WHEN** native submit 成功而 present 返回 OutOfDate
- **THEN** 外层 MUST 成功，资源/状态 MUST commit，并标记后续 presentation rebuild

#### Scenario: Submit 成功 present Suboptimal
- **WHEN** native submit成功而present返回Suboptimal
- **THEN** 外层 MUST成功并携带有效completion，presentation_status MUST为Suboptimal且后续有效frame安排rebuild

#### Scenario: Submit 成功 present terminal
- **WHEN** native submit成功而present返回DeviceLost或其他不可恢复错误
- **THEN** 外层 MUST仍表达业务已提交并携带有效completion/terminal presentation_status；Renderer MUST先commit业务状态，再进入terminal

#### Scenario: Submit 失败
- **WHEN** 业务 lists 未成功 submit
- **THEN** 外层 MUST 失败，不得返回可用于业务 commit 的 completion

#### Scenario: Submit 边界未知
- **WHEN** backend failure无法证明业务work未进入queue
- **THEN** viewport/device MUST terminal，禁止把local/committed/resource状态回滚后继续下一帧

### Requirement: Submit 后发布不可失败
所有command list、device identity、resource state、binding、pipeline、frame ownership和payload retain validation MUST在native submit前完成；submit成功后的mark-submitted、queue committed state、RenderResource Ready、candidate active publication、last-use、in-flight strong refs和completion publication MUST不返回可恢复失败。

#### Scenario: Submit 后发现内部不变量破坏
- **WHEN** native submit 已成功
- **THEN** RHI MUST 报告已提交及 completion，同时锁存 terminal，绝不得伪装成未提交

### Requirement: Abort 不提交业务工作
acquire成功后的resource recording、`init_views()`、visibility、pass或`finish_recording()`失败 MUST通过 `abort_frame()`闭合frame。abort MUST丢弃所有业务command list和command-list local final state，RenderResourceManager MUST discard当前recording collection；backend为消费acquire synchronization、推进frame slot或present image所做的最小同步submit MUST NOT视为业务submit，不得发布resource Ready/candidate active/queue committed final state，也不得返回业务completion。

#### Scenario: init_views 失败
- **WHEN** begin_frame/acquire成功而init_views检测到无效view input
- **THEN** Renderer MUST不录制测试/Base Pass，丢弃当前业务recording并把FrameContext交给abort_frame闭合

#### Scenario: Abort 成功
- **WHEN** recording failure后的abort最小同步闭环成功
- **THEN** frame slot MAY在满足backend completion规则后复用，pending resources保持可重录且本次不产生业务commit

#### Scenario: Abort 闭环失败
- **WHEN** backend 无法安全消费 acquire synchronization 或推进 frame slot
- **THEN** viewport/device MUST terminal，后续不得复用状态未知对象

### Requirement: 跨后端实现
contract MUST 可由 Vulkan、D3D12、D3D11 FL11_0 与 VulkanPortable v1 实现；swapchain、semaphore、fence、queue family、immediate context 等原生细节不得泄漏到 RenderScene。

Vulkan SHALL把graphics queue submit与present作为两个结果边界，semaphore/fence保持backend私有。D3D12 SHALL分别映射command queue execute、DXGI present和fence value。D3D11 SHALL在RT queue submit阶段串行执行deferred/immutable packet、独立调用DXGI present，并使用FL11_0 event query或等价GPU signal生成completion。VulkanPortable v1不得依赖Vulkan 1.2+或可选同步特性。

#### Scenario: D3D11 frame submit
- **WHEN** D3D11 backend 提交录制单元
- **THEN** 工作 MUST 在 RT queue submit 阶段串行执行，present 与 GPU completion 仍按公共结果区分
