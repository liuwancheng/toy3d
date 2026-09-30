# Spec Delta

## Purpose

定义 Toy3d 创作资产的 YAML 描述、可选处理后数据、成对操作和故障恢复行为。

## ADDED Requirements

### Requirement: 描述资产采用有界 YAML

`.asset` MUST 是 UTF-8 YAML 描述，包含格式版本、Asset ID、根类型、schema 版本、直接依赖、子资源和类型属性。相同逻辑内容 MUST 产生确定性输出。解析 MUST 限制总字节、节点数量、深度、字符串与数组长度，并拒绝重复键、别名、自定义标签、非法数值与无效 UTF-8；失败 MUST 保持已加载对象不变。

#### Scenario: 编辑描述字段

- **WHEN** 用户只修改一个材质参数并保存
- **THEN** `.asset` MUST 可作为文本审阅，保存 MUST 不产生二进制属性段或缩略图字节

### Requirement: 处理后数据使用可选同名 meta

需要处理后字节的资产 MUST 有同名 `.meta`，`.asset` MUST 记录 meta 格式、大小和 SHA-256；`.meta` MUST 记录相同 Asset ID 及有界段目录。纯描述资产 MUST 仅有 `.asset`；模型与贴图的必要数据 MUST 分别可从 `render_geometry` 与 `texture_mips` 段读取。原始外部文件及其完整中间表示 MUST 不被持久化。

#### Scenario: 缺失处理后数据

- **WHEN** 模型 `.asset` 指向的同名 `.meta` 缺失或摘要不匹配
- **THEN** Catalog/loader MUST 报告错误且不得使用其他资产的 meta 或半成品数据

### Requirement: 身份和依赖与路径分离

Asset ID、Subresource ID、预期类型及引用强度 MUST 在保存和移动时保留。Catalog MUST 只枚举 `.asset` 作为资产；孤立 `.meta` MUST 诊断且不得显示为独立资产。强依赖缺失或成环 MUST 保持现有可诊断失败；反向引用 MUST 可供 Editor 删除检查。

#### Scenario: 重命名模型

- **WHEN** Editor 将 `A.asset` 重命名为 `B.asset`
- **THEN** 同名 `A.meta` MUST 随之成为 `B.meta`，Asset ID 和既有引用 MUST 不变

### Requirement: 整体操作可恢复

创建、保存、重命名、移动、复制和删除 MUST 按资产整体执行；有 meta 的资产不得只更新一侧。更新 MUST 先验证完整候选，再以描述文件为提交点发布。Editor MUST 在 Catalog 扫描前恢复可识别的中断事务；不可判定时 MUST 保留数据并报告。失败 MUST 保留可恢复的旧版本，不得把不匹配的一对文件视作成功。

#### Scenario: 替换 meta 后进程中断

- **WHEN** 新 `.meta` 已替换而新 `.asset` 尚未发布
- **THEN** 下次启动 MUST 恢复旧配对或报告需要人工处理，不能以旧描述配新数据加载

### Requirement: 缩略图为可再生成缓存

缩略图 PNG 和失效签名 MUST 不写入 `.asset` 或 `.meta`。Editor MAY 在 `/Saved/AssetThumbnails/` 缓存缩略图；缓存缺失、损坏或过期 MUST 允许重建，不得影响资产有效性。运行时 MUST 不依赖缩略图缓存。

#### Scenario: 清空 Saved

- **WHEN** 用户删除整个缩略图缓存
- **THEN** `.asset`/`.meta` MUST 保持有效，Content Browser MUST 能重新生成所需缩略图

### Requirement: 旧二进制 Asset 不受支持

Catalog、Editor 和 runtime MUST 拒绝旧 `TOY3DAST` 单文件资产。新建资产 MUST 直接生成 YAML 描述和可选 meta，不得先生成旧文件再转换。生产构建 MUST 不包含旧格式转换器或迁移工具。

#### Scenario: 打开旧二进制资产

- **WHEN** 工作区包含旧 `TOY3DAST` `.asset`
- **THEN** Catalog MUST 报告格式错误，且保持上次有效索引
