## Purpose

在新框架实现前明确识别并移除会与新设计形成双轨的 Game/Render transport、资源镜像、ID/revision 和空壳 RHI 命令原型，为后续 capability 提供单一迁移起点。

## Type Contracts

本 capability 不新增运行时类型；其目标是删除废弃类型及其正式入口。

## ADDED Requirements

### Requirement: 建立可验证的废弃清单
清理阶段 MUST 盘点旧 frame transport、RenderCommand 原型、资源 cache/collector、typed render-resource ID/revision、旧 Scene frame processor、空壳 device command list 及其测试和 CMake target。

#### Scenario: 清理开始
- **WHEN** 执行该 capability
- **THEN** 清单 MUST 为每个废弃符号给出定义、调用方、测试、构建目标和替代 capability

### Requirement: 删除旧正式执行路径
旧 `RenderFramePacket`、dispatcher、私有 queue/completion、旧 resource cache/collector 与空壳 RHI device command list MUST 从正式运行时和测试入口移除，不得保留 adapter。

#### Scenario: 清理完成
- **WHEN** 运行全仓符号搜索和受影响目标构建
- **THEN** 废弃清单中的运行时类型 MUST 无定义和调用残留，只有 archive/OpenSpec 说明 MAY 引用旧名称

### Requirement: 保留可复用底层能力
清理 MUST 保留 Task Graph named queue、RHI command-list local state、queue completion、deferred deletion、viewport abort 和 device bootstrap 等已符合新 contract 的基础。

#### Scenario: 删除旧上层路径
- **WHEN** resource cache 或 frame dispatcher 被删除
- **THEN** 底层 RHI upload、command context、queue completion 和 Task Graph 测试 MUST 继续可独立构建
