## Purpose

定义 logical Rendering Thread 内 RHIDevice、RenderResourceManager、device bootstrap placeholder 与 primary viewport 的初始化、发布和失败回滚顺序。

## Type Contracts

本 capability 不新增独立运行时类型；它编排 `Renderer`、现有 RHI 类型和 `RenderResourceManager`。初始化阶段状态由 `RendererLifecycleState`（归属 terminal/shutdown capability）表达。

## ADDED Requirements

### Requirement: Bootstrap 顺序固定
RT bootstrap MUST 按 RHIDevice、RenderResourceManager、placeholder device submission、completion wait、primary viewport 的顺序执行，全部成功后才发布 Renderer Running。

#### Scenario: 正常启动
- **WHEN** Window/Surface、Task Graph 与 RenderingThread ready
- **THEN** bootstrap MUST 完成 placeholder GPU 可用性和 viewport 创建后才开放 frame/resource façade

### Requirement: Placeholder 全有或全无
placeholder 创建、录制、submit 或 completion wait 任一步失败 MUST 导致整个 Renderer bootstrap 失败；不得发布部分 placeholder set。

#### Scenario: Placeholder submit 失败
- **WHEN** device-level command list 未成功 submit
- **THEN** bootstrap MUST 保留原始错误并释放未发布 refs，Renderer MUST 不进入 Running

### Requirement: Bootstrap 使用显式 device context
placeholder MUST 使用非 viewport device-level graphics context 录制 upload/transition并显式 queue submit；resource create MUST 不隐式 submit 或 wait。

#### Scenario: Bootstrap 录制
- **WHEN** 创建 placeholder texture
- **THEN** create empty resource、upload、transition、finish、submit、wait MUST 是显式步骤

### Requirement: 失败逆序回滚
bootstrap failure MUST 只销毁已成功创建的对象，顺序为 viewport（若有）、placeholder refs、resource manager、device；原始错误不得被 cleanup 错误覆盖。

#### Scenario: Primary viewport 创建失败
- **WHEN** device 与 placeholder 已成功但 viewport 创建失败
- **THEN** bootstrap MUST 释放 placeholder、清 manager、shutdown device，并返回 viewport 原始错误

### Requirement: 普通资源不复用 bootstrap wait
Renderer Running 后普通 Mesh/Texture/Material init SHALL 进入 frame-local pending upload，不得使用 bootstrap 的同步等待路径。

#### Scenario: Gameplay 加载 Texture
- **WHEN** Texture 在正常运行期 begin init
- **THEN** 调用 MUST fire-and-forget，资源在后续业务 submit 成功后 Ready
