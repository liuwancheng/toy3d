# Spec Delta

## Purpose

以模型、动画、物理碰撞、场景和材质的代表性需求约束共享资源基础设施，确保反射与 Asset 文件 API 不会仅围绕单一属性面板设计，并为后续各领域的完整导入器、编辑器和运行时接入留下可验证边界。

## ADDED Requirements

### Requirement: 模型资源区分来源、设置与子资源
模型创作数据 MUST能描述源文件身份、导入设置、mesh/material/skeleton 等有稳定身份的子资源及其覆盖项；几何大块数据 MUST保留独立产物引用和目标 profile 验证入口，而不进入普通属性树。本 change 用代表性 Asset 文件验证该边界，不交付生产几何导入器。通用反射 MUST不把 `StaticMesh` 的运行时共享指针、RenderData 或 RHI buffer 作为源数据保存。

#### Scenario: 模型重导入后材质槽保留
- **WHEN** 源模型重新导入且同一 mesh 子资源的顺序变化
- **THEN** 已针对该子资源保存的材质槽覆盖 MUST仍按稳定身份匹配，失配 MUST报告而非按新顺序套用

### Requirement: 动画资源表达目标与时间数据
动画创作数据 MUST能描述骨架/目标资源引用、轨道身份、时间与插值数据及事件；代表性 Asset 文件的领域验证 MUST检查关键帧时间范围、排序、目标可解析性及数值合法性。通用序列化 MUST支持这些结构的稳定往返，但 MUST不把采样或压缩策略下沉为通用反射行为。

#### Scenario: 无效轨道目标
- **WHEN** 动画轨道引用不存在的骨骼或场景对象目标
- **THEN** 领域验证 MUST报告轨道和目标身份，不能把轨道静默绑定到同名的其他目标

### Requirement: 碰撞资源使用受控形状分支
碰撞创作数据 MUST能表达 box、sphere、capsule 等参数化形状及 convex/triangle mesh 等需 Cook 的几何引用；每个形状 MUST有明确分支标签、局部变换和领域参数校验。物理后端对象和加速结构 MUST不由通用反射直接构造或持久化。

#### Scenario: Capsule 尺寸非法
- **WHEN** 碰撞资源包含半径非正的 Capsule
- **THEN** 领域验证 MUST拒绝该形状并定位到对应元素，不得交给物理后端自行修正

### Requirement: 场景资源表达对象图而非内存图
场景创作数据 MUST能表达稳定 Actor/Component 身份、类型、可持久化属性、root/attachment 关系及外部资源引用；本 change 的场景 Asset 文件验证 MUST检查身份唯一、引用存在和附件无环。后续运行时装配 MUST由 GameScene 领域入口创建和连接对象。反射/编解码层 MUST不直接构造或删除 Actor/Component，不得序列化指针、World 帧时间、注册状态或 RenderScene 镜像。

#### Scenario: 跨 Actor 附件
- **WHEN** 子 SceneComponent 附着到同一 World 中另一 Actor 的组件
- **THEN** 场景类型数据 MUST以场景内身份描述该关系并通过两端存在及无环验证；后续装配 MUST据此恢复关系

#### Scenario: 不存在的父组件
- **WHEN** 场景附件引用缺失的组件身份
- **THEN** 场景数据验证 MUST失败且不得发布候选；后续装配 MUST保留当前 World 的可用状态，不得形成悬空 attachment

### Requirement: 材质属性不复制 Shader schema
材质创作数据可保存资源身份、Shader 引用及参数覆盖，但参数名称、类型、默认值和显示约束 MUST以已验证的 `.shader Properties` schema 为权威。通用反射 MUST不重新分配 `ShaderParameterId`、重算 GPU layout 或让检查器依赖 RHI native slot；普通参数编辑与结构性变更 MUST遵守现有 Material replacement 边界。

#### Scenario: Shader 参数改名
- **WHEN** 材质覆盖引用已从 Shader schema 删除或改名的参数
- **THEN** 编辑器 MUST保留 orphan 覆盖并显示明确错误，或通过显式迁移处理，不得把它绑定到同槽位的新参数
