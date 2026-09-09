## Purpose

定义 ShaderCompiler、ShaderFormat、RenderCore 与公共 RHI 共同使用的稳定 binding 身份、constant-buffer 数据 ABI 身份和 Program active target mapping，使逻辑资源不依赖任一 Program 的物理 slot。

## ADDED Requirements

### Requirement: Shader binding 使用唯一稳定身份
每个 constant buffer 聚合绑定和独立 shader resource MUST 使用同一个 canonical `ShaderParameterId` contract 标识；ShaderCompiler、ShaderFormat、RenderCore 与 RHI MUST 共享同一 value type 和生成规则，不得定义可互相转换但语义重复的第二套 binding ID。ID MUST 由 logical group、parameter category 和 canonical name 生成，constant member ID 与其所属聚合 constant-buffer binding ID MUST 保持不同身份。

#### Scenario: 同一资源跨 Program 编译
- **WHEN** 两个 Program 使用相同 logical group、category 和 parameter name，但 target allocator 为它们分配不同 slot
- **THEN** 两个 Program MUST 保存相同 ShaderParameterId，并分别保存自己的 target mapping

#### Scenario: ShaderParameterId collision
- **WHEN** Cook 或 Editor publication 发现两个不同 canonical binding description 生成相同非零 ID
- **THEN** 产物 MUST 可诊断地拒绝，不得依靠名称、声明顺序或 target slot 猜测身份

### Requirement: Program active layout 同时携带逻辑身份与目标映射
每个 active Program binding record MUST 包含 ShaderParameterId、logical group、resource type、array count、stage visibility 和当前 target mapping。Uniform buffer record MUST 额外包含 canonical byte size、完整 data layout hash 与 ToyShaderABI version；target slot MUST NOT 作为逻辑资源身份或资产侧参数 key。

#### Scenario: Constant buffer 尺寸相同但布局不同
- **WHEN** logical BindingSet 提供的 uniform buffer 与 Program 要求具有相同 byte size、但 data layout hash 或 ToyShaderABI version 不同
- **THEN** runtime MUST 在 native binding 前拒绝该组合

#### Scenario: 多 stage target slot 不同
- **WHEN** 同一个 ShaderParameterId 在 vertex 和 pixel stage 映射到不同 D3D register slot
- **THEN** Program MUST 保留两个 stage-specific target mapping，logical resource owner MUST 仍只提供一份资源值

### Requirement: Active layout 与完整 schema 分离
Program active layout MUST 只记录最终字节码实际使用的独立资源；一个 logical group 的任一 constant member 被使用时，Program MUST 保留该 group 的完整 canonical constant buffer及其稳定成员 offset。Runtime logical BindingSet MAY提供完整 schema 的资源 superset，Program active layout不得因未使用资源改变已使用资源的逻辑身份。

#### Scenario: Material variant 裁剪纹理
- **WHEN** 一个 Material variant 未使用完整 Material schema 中的某张纹理
- **THEN** 该纹理 MUST 不进入 Program active layout，但其他 active binding 的 ShaderParameterId 和 constant layout MUST 保持稳定

### Requirement: Binding metadata 版本破坏性迁移
ShaderMapEntry、target mapping 和相关 runtime record MUST 提升足以覆盖 binding identity、constant data layout 和 mapping semantics 的版本。Runtime MUST 明确拒绝缺少 required identity/layout metadata 的旧产物；本 change MUST NOT 保留旧 reader、默认补字段或双版本正式加载路径。

#### Scenario: 加载旧 ShaderMapEntry
- **WHEN** runtime 加载未包含 required binding ID 或 constant layout identity 的旧版本 Entry
- **THEN** 加载 MUST 返回可诊断的 version/format failure，并要求重新编译 Shader 产物

#### Scenario: 仅 target mapping 改变
- **WHEN** Shader hot reload 保持 active binding ID、类型、数组和 constant ABI 不变，只改变 target mapping
- **THEN** 新 Program MUST 重建对应 RHI layout/pipeline，但现有 logical BindingSet MUST 可继续使用

