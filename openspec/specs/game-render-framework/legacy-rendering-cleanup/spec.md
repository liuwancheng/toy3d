# game-render-framework/legacy-rendering-cleanup Specification

## Purpose
在新框架实现前明确识别并移除会与新设计形成双轨的Game/Render transport、Scene frame processing、资源镜像、typed ID/revision和空壳RHI命令原型，为后续capability提供单一迁移起点，并禁止用adapter、alias或no-op success维持旧正式入口。

## Requirements

### Requirement: 建立可验证的废弃清单
清理阶段 MUST盘点旧frame transport、RenderCommand原型、资源cache/collector、typed render-resource ID/revision、旧Scene frame processor/Camera builder、空壳device command list及其测试和CMake target。清单 MUST记录定义、正式调用方、测试、构建入口和替代capability；迁移期间不得再增加新的旧路径调用方。

#### Scenario: 清理开始
- **WHEN** 执行该 capability
- **THEN** 清单 MUST 为每个废弃符号给出定义、调用方、测试、构建目标和替代 capability

#### Scenario: 替代 capability 尚不完整
- **WHEN** 正式调用方仍有必要语义无法由确认的新capability表达
- **THEN** 清理 MUST暂停该entry并先完善替代路径，不得放置no-op adapter、compatibility wrapper或第二套queue/cache使删除表面完成

### Requirement: 清理按调用与构建依赖顺序执行
每个retired entry SHALL按清单、冻结新增调用、迁移/删除正式调用方、删除源码、删除CMake source/test target、全仓符号检查和保留目标构建的顺序收敛。删除动作不得早于最后一个正式调用方迁移，也不得把旧测试fixture保留为新框架的隐式依赖。

#### Scenario: 删除旧源码前仍有调用方
- **WHEN** `rg`在runtime、editor、tools或正式测试中仍找到旧符号调用
- **THEN** 该entry MUST保持未完成并先迁移调用方，不能通过forward declaration、typedef或stub满足编译

#### Scenario: CMake仍登记旧target
- **WHEN** 源码已删除但CMake仍包含旧source或test target
- **THEN** 清理 MUST同步删除该登记并修复依赖方，不能生成空target维持旧名称

### Requirement: 删除旧正式执行路径
旧`RenderFramePacket`/Queue/Dispatcher/Completion、Scene update batch/frame processor/Camera builder、`RenderResourceCache`/collector、typed render-resource ID/revision、带空壳`RHIDeviceCommandList`参数的RenderCommand原型 MUST从正式运行时、公共头文件、测试入口和CMake移除，不得保留adapter、alias、wrapper或并行fixture。

#### Scenario: 清理完成
- **WHEN** 运行全仓符号搜索和受影响目标构建
- **THEN** 废弃清单中的运行时类型 MUST 无定义和调用残留，只有 archive/OpenSpec 说明 MAY 引用旧名称

#### Scenario: 只有历史文字命中
- **WHEN** 全仓搜索只在当前OpenSpec或`document/archive/`路径报告retired名称
- **THEN** 验证 SHALL把它记录为允许的历史文本，不得读取archive并据此恢复旧设计或把历史命中误判为正式代码残留

#### Scenario: 正式target删除后构建失败
- **WHEN** 保留的Editor/Core/RHI target因旧target删除而丢失必要依赖
- **THEN** 实现 MUST完成真实替代依赖迁移，不能新增no-op implementation或空壳success adapter使构建假绿

### Requirement: 保留可复用底层能力
清理 MUST保留Task Graph named queue/GraphTask/GraphEvent、RHI command-list local state、queue committed state/completion、upload/transition、deferred deletion、viewport acquire/abort/end-frame和device启动引导等已符合新contract的基础。删除旧`RHIDeviceCommandList`只删除缺乏真实ownership、recording与submit语义的空壳原型，不得删除正式RHI command context/list能力。

#### Scenario: 删除旧上层路径
- **WHEN** resource cache 或 frame dispatcher 被删除
- **THEN** 底层 RHI upload、command context、queue completion 和 Task Graph 测试 MUST 继续可独立构建

#### Scenario: 删除空壳 command list
- **WHEN** 旧`RHIDeviceCommandList`和带该参数的RenderCommand原型被删除
- **THEN** 公共RHI的正式command context、immutable command list、queue submit、state reconciliation与completion contract MUST保留且继续独立验证

### Requirement: 删除旧测试不按数量补测
只验证retired architecture shape的packet/cache/collector/prototype测试 SHALL随旧代码删除，不得为了维持测试数量为每个旧类型复制一套新fixture。Task Graph与RHI底层行为继续由现有Core/RHI测试负责；Game→Render整体交互由Batch D共享纵向E2E负责。

#### Scenario: 旧packet fixture被删除
- **WHEN** fixture只构造`RenderFramePacket`并验证旧dispatcher/queue
- **THEN** fixture和target MUST删除，不得改名后继续测试不存在的packet边界；新SceneView/SceneRenderer行为在整体framework闭合后集中覆盖

#### Scenario: 底层能力仍被复用
- **WHEN** 清理删除上层resource cache测试
- **THEN** 既有RHI upload/state/completion测试 MUST保留，新RenderResourceManager集成只在其纵向batch增加少量smoke
