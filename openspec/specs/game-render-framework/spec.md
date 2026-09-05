# game-render-framework Specification

## Purpose
作为 Toy3d Game/Render 框架的总控 capability，定义跨线程、Scene、资源与 RHI 子 capability 的依赖关系、端到端帧行为、类型治理和分阶段完成门槛，供多次 apply 持续定位工作。

## Requirements

### Requirement: 子 capability 是执行边界
每个子 capability SHALL 拥有独立 requirements、类型清单、任务组和完成门槛；下游 capability MUST NOT 在其依赖 capability 未通过验证前成为正式入口。相邻且依赖连续的 capability MAY 在同一较大施工批次内完成纵向框架；批次中间只要求 configure、受影响正式 target build 和少量 smoke，达到批次发布点时再集中执行对应完整测试矩阵。

#### Scenario: 多次 apply 继续执行
- **WHEN** 新一次 apply 启动
- **THEN** 执行者 MUST 从当前 Batch 中选择一组依赖连续的未完成任务，优先形成 Scene→View→Resource→Base Pass 的纵向闭环，不得为了单独验收空壳而扩张测试接口

### Requirement: 新增类型必须先登记
实现期间新增的每个第一方 class、struct、enum、alias 或其他具名类型 MUST 先确认 UE4.27 对应术语、Toy3d 实际职责和最终名称，再登记在唯一所属子 Spec 的 `Type Contracts`，并说明职责、所有权、可变线程、创建销毁线程、错误语义和为何不能使用已有类型。

#### Scenario: 实现发现缺少类型
- **WHEN** apply 过程中发现当前 Type Contracts 未覆盖所需具名类型
- **THEN** 实现 MUST 暂停该类型的代码落地，先更新对应 Spec 并重新验证 change

#### Scenario: 实现私有辅助类型
- **WHEN** 新增类型只在 `.cpp` 内使用
- **THEN** 该类型仍 MUST 登记；标准库类型、lambda closure 和编译器生成类型不需要登记

#### Scenario: 候选名称尚未确认
- **WHEN** 设计发现候选类型但其最终名称尚未确认
- **THEN** 候选名 MUST NOT 写入 planning artifact、代码或正式测试接口

### Requirement: 代码型 Spec 必须提供最小实现示例
每个新增或修改第一方运行时代码的子 capability spec MUST 包含 `Minimal Implementation Example`，说明 CPU owner、non-owning observer 或 ownership transfer、GT/RT 可变线程、主要调用顺序和至少一个失败路径。示例 MUST 只使用已确认类型名，并且属于 non-normative 内容；若与 `Type Contracts` 或 requirements 冲突，以规范性内容为准。

#### Scenario: Spec 新增运行时类型或调用路径
- **WHEN** 子 capability 新增运行时类型或正式调用路径
- **THEN** 同一 Spec MUST 提供对应的最小实现示例

#### Scenario: 示例需要未确认类型
- **WHEN** 编写示例需要引用尚未确认的新类型
- **THEN** 示例编写 MUST 暂停，先完成名称确认和 Type Contract 登记

### Requirement: 一帧端到端顺序
正常帧 MUST 按 Game updates、`SceneView`/`SceneViewFamily` 一次性输入、RT FIFO Proxy/Resource 更新、`init_views()`、`compute_view_visibility()`、`MeshBatch` 收集、`LocalVertexFactory` 与 `ShaderVertexInput` 匹配、Forward Base Pass、业务 submit、presentation 与独立 GPU completion 的顺序执行，且 CPU Fence、submit commit 和 GPU completion MUST 保持不同语义。

#### Scenario: 正常 frame N
- **WHEN** Game Thread 完成 frame N 的 World tick、状态更新和 Draw 投递
- **THEN** logical Rendering Thread MUST 在 Draw 前应用 FIFO 更新，成功 submit 后发布资源状态，并由独立 completion 控制 GPU payload 回收

### Requirement: 不建立兼容双轨
迁移 SHALL 在每个阶段保持一个正式入口；旧 transport、旧资源 registry 或旧 frame submit 路径 MUST NOT 与新路径长期同时可用。

#### Scenario: 新 capability 发布
- **WHEN** 一个新 capability 达到完成门槛
- **THEN** 对应旧正式入口 MUST 在同一迁移批次删除或变为不可构建

### Requirement: Game 与 Render 可变状态分侧
除现有 `engine/runtime/engine.h::toy3d::Engine` 作为 GT composition root 外，框架模块 MUST 明确归属 Game side 或 Render side；跨侧 bridge MUST 只表达命令、同步和生命周期 contract，不得拥有任一侧的业务可变状态。

#### Scenario: 新增模块或类型
- **WHEN** apply 需要新增模块或具名类型
- **THEN** 对应 Type Contracts MUST 标明 Game side、Render side 或 stateless bridge，且不得引入新的 Engine 抽象层
