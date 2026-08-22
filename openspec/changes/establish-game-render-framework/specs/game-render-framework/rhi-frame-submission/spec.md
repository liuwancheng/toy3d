## Purpose

定义 viewport begin/end/abort、业务 command list submit 与 presentation 的跨后端结果语义，使上层能准确决定资源 commit、重试、resize 与 terminal。

## Type Contracts

| Type | 状态 | 职责与边界 |
| --- | --- | --- |
| `RHIFrameEndResult` | 新增 struct | 外层成功时携带有效 `RHIQueueCompletionValue` 与独立 `presentation_status`；不暴露 backend token |

沿用现有 `RHIFrameContext`、`RHIViewportContext`、`RHIGraphicsCommandContext`、`RHICommandList`、`RHIStatus` 和 `RHIResult`，不新增 Immediate command list。

## ADDED Requirements

### Requirement: 单 viewport 单录制单元
第一阶段每个 viewport Draw SHALL 创建一个 graphics context，并把 pending uploads 与全部 graphics pass 串行录制为一个 immutable command list；end frame 接口 MUST 保留未来接收多个 lists 的形状。

#### Scenario: Upload 后 Draw
- **WHEN** 本帧 Mesh buffer upload 后立即被 Draw 使用
- **THEN** upload、transition 和 Draw MAY 在同一 list 中，CPU 不等待 GPU

### Requirement: Submit 与 present 分离
frame-end 外层成功 MUST 表示业务 lists 已成功 submit 并返回有效 completion；presentation status MUST 独立表达 Success、Suboptimal、OutOfDate 或 terminal。

#### Scenario: Submit 成功 present OutOfDate
- **WHEN** native submit 成功而 present 返回 OutOfDate
- **THEN** 外层 MUST 成功，资源/状态 MUST commit，并标记后续 presentation rebuild

#### Scenario: Submit 失败
- **WHEN** 业务 lists 未成功 submit
- **THEN** 外层 MUST 失败，不得返回可用于业务 commit 的 completion

### Requirement: Submit 后发布不可失败
所有可失败 validation MUST 在 native submit 前完成；submit 成功后的 mark-submitted、state/resource commit、last-use 和 in-flight retain MUST 不返回可恢复失败。

#### Scenario: Submit 后发现内部不变量破坏
- **WHEN** native submit 已成功
- **THEN** RHI MUST 报告已提交及 completion，同时锁存 terminal，绝不得伪装成未提交

### Requirement: Abort 不提交业务工作
acquire 成功后的 recording failure MUST 通过 abort 闭合 frame；backend 最小同步 submit MUST NOT 视为业务 submit或发布资源 Ready/final state。

#### Scenario: Abort 闭环失败
- **WHEN** backend 无法安全消费 acquire synchronization 或推进 frame slot
- **THEN** viewport/device MUST terminal，后续不得复用状态未知对象

### Requirement: 跨后端实现
contract MUST 可由 Vulkan、D3D12、D3D11 FL11_0 与 VulkanPortable v1 实现；swapchain、semaphore、fence、queue family、immediate context 等原生细节不得泄漏到 RenderScene。

#### Scenario: D3D11 frame submit
- **WHEN** D3D11 backend 提交录制单元
- **THEN** 工作 MUST 在 RT queue submit 阶段串行执行，present 与 GPU completion 仍按公共结果区分
