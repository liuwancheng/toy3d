# Spec Delta

## Purpose

定义供资源创作数据、编辑器和离线工具共享的 opt-in 类型与属性描述，使同一份稳定 schema 能驱动编辑、版本化读写和验证，同时不取得运行时对象的所有权或生命周期控制权。

## ADDED Requirements

### Requirement: 只有显式声明的数据进入资源反射
资源反射 MUST只暴露显式选择的创作数据类型和属性；未登记的 C++ 字段、方法、指针、图形句柄和线程状态 MUST不因继承、内存布局或命名惯例自动进入持久化或编辑器。构建生成的描述 MUST与原始字段类型一致；不支持的类型 MUST在生成或注册阶段报可定位错误。

#### Scenario: 运行时字段未声明
- **WHEN** 创作数据类型包含一个未声明为可持久化的运行时缓存字段
- **THEN** 保存与编辑器枚举 MUST都不包含该字段

#### Scenario: 不支持的字段类型
- **WHEN** 被声明的属性是原始指针或未获支持的容器
- **THEN** 生成或注册 MUST失败并定位到类型和字段，不得静默跳过或保存指针数值

### Requirement: 稳定身份独立于 C++ 实现细节
类型与属性 MUST具有明确的持久化名称。重排字段、更改 C++ 命名空间或生成代码顺序 MUST不改变既有文件身份；重命名持久化名称 MUST经显式迁移或别名规则。重复名称、冲突别名或同名不同类型 MUST在注册完成前失败。

#### Scenario: 字段重排
- **WHEN** 两个构建仅改变同一类型的 C++ 字段声明顺序
- **THEN** 两者 MUST识别相同的持久化字段身份并能读取已有文档

#### Scenario: 重复身份
- **WHEN** 两个模块登记相同持久化类型名称但描述不一致
- **THEN** 注册 MUST返回明确错误，不得依赖链接顺序选择其一

### Requirement: 创作属性默认保存并独立声明编辑用途
显式声明的创作数据属性 MUST默认参与 Toy3d Asset 文件对应类型数据段的序列化和反序列化，除非声明 `Transient`。初版用途标记 MUST包括 `Edit`、`Visible`、`Transient`：`Edit` 允许属性修改，`Visible` 仅允许只读展示，`Transient` 排除文件读写；未标记为 `Edit` 或 `Visible` 的已声明属性 MUST仍能保存但不进入普通检查器。`Edit` 与 `Visible` MUST互斥；初版资源编辑 MUST拒绝 `Edit | Transient`，并在错误中标明类型与字段。`Category`、`Range`、`Unit`、`AssetType` MUST作为可选提示而非用途标记，提示 MUST不代替类型和领域校验。

#### Scenario: 派生只读值
- **WHEN** 属性声明为 `Visible | Transient`
- **THEN** 属性查询 MUST标明只读，保存 MUST忽略它且编辑请求 MUST被拒绝

#### Scenario: 隐藏持久化值
- **WHEN** 创作数据属性显式声明但没有 `Edit`、`Visible` 或 `Transient`
- **THEN** 读写 MUST保留该值，普通属性列表 MUST不把它作为可编辑项

#### Scenario: 可编辑导入设置
- **WHEN** 模型导入设置属性声明为 `Edit` 且没有 `Transient`
- **THEN** 属性 MUST可编辑并参与对应类型数据段的序列化和反序列化，无需额外的 `Serialize` 标记

#### Scenario: 非法用途组合
- **WHEN** 属性同时声明 `Edit` 与 `Visible`，或同时声明 `Edit` 与 `Transient`
- **THEN** 生成或注册 MUST失败并定位到类型和字段

### Requirement: 支持资源数据所需的结构形态
schema MUST支持固定宽度数值、布尔、UTF-8 文本、稳定枚举值、Core Math 值、嵌套结构、受限数组与按明确标签区分的变体。数组元素与变体分支 MUST保留类型信息；引用 MUST以专用身份值表达。任意对象指针图和未登记多态派生类型 MUST不被自动遍历。

#### Scenario: 碰撞形状变体
- **WHEN** 碰撞文档中同一数组含 Box 与 Capsule 两种已登记形状
- **THEN** 每个元素 MUST保存明确分支身份并能还原对应参数

#### Scenario: 未知变体分支
- **WHEN** 文档使用当前注册表未知的必需形状分支
- **THEN** 读取 MUST报告该分支的路径并拒绝将其解释为另一种形状

### Requirement: 注册结果有明确生命周期
各模块 MUST在使用前显式提交其描述并完成冲突检查；完成后的描述 MUST对并发只读查询保持稳定。未完成注册、失败注册及卸载中的类型 MUST不暴露半成品描述。生成代码 MUST能够从构建目录使用，且不得依赖静态初始化顺序或生产源码目录中的生成文件。

#### Scenario: 查询冻结后的类型
- **WHEN** 资源加载与编辑器同时查询已完成注册的类型
- **THEN** 两者 MUST观察到同一份不可变属性集合
