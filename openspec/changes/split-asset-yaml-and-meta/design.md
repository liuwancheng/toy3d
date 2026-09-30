## Context

原 `Toy3dResource` 以 `TOY3DAST` v1 单文件保存 `type_data`、`render_geometry`、`texture_mips`、`source_mesh`、`import_data` 和缩略图等段；共享文件系统的原子性只覆盖单文件。用户确认不需要旧格式兼容。此前已手工处理工作区里的四个资产，原字节备份保留在资产目录外。

## Goals / Non-Goals

- 目标：描述可读、稳定可审阅；处理后数据与描述分离；纯描述资产仅一文件；资产引用稳定；失败后不暴露半成品；迁移不丢用户数据。
- 非目标：保存 FBX/PNG 原件、通用多文件文件系统事务、跨进程瞬时原子观察、把缩略图作为权威数据、在本次改动中引入 Cook/package 系统。

## Decisions

### 文件与身份

`<name>.asset` 是 UTF-8 YAML，格式版本独立于领域 schema 版本。顶层键固定为 `format_version`、`asset_id`、`root_type`、`schema_version`、`dependencies`、`subresources`、`data`，有处理后数据时增加 `meta`。`data` 用持久化字段名表达领域属性；`AssetRef` 以 ID、可选子资源 ID、预期类型和强/弱/延迟语义表达。输出固定键顺序、稳定列表顺序、数值和字符串写法及换行；读取拒绝重复键、未知必需结构、别名/自定义标签、无效 UTF-8、非有限数、超限深度/数量/字节。未知可选内容无法无损保留时只读打开。

`meta` 包含格式版本、文件大小、SHA-256 和必需段名称。无 `meta` 键表示纯描述资产，且同名 `.meta` 必须不存在。`<name>.meta` 是二进制有界分段容器，头部记录 magic、格式版本、Asset ID 和段目录；每个段有名称、长度和边界。模型保留 `render_geometry`，纹理保留 `texture_mips`；后续领域按需定义其他处理后段。`.meta` 不保存原始导入文件、`source_mesh` 或旧 `import_data`。当前导入设置不作为正式资产字段；再次导入需重新选择源文件并输入设置，迁移备份保留旧记录供人工查阅。

### API 与读写

`Toy3dResource` 增加描述解析/编码、meta 容器读写、成对发布和恢复；yaml-cpp 仅为实现依赖，领域与 Editor 不直接使用 YAML 节点。`read_asset_pair()` 验证 YAML、meta 头、段边界及摘要，再将完整候选交给领域解码器校验；大段的读取上限由领域传入。`AssetIndex` 继续以 Asset ID 管理身份与正向依赖；Editor 从 Catalog 构建反向引用视图供删除提示。保存需比较发布基线并保持未知数据的无损性。

### 成对发布与恢复

资产层按同一个 Editor owner 串行化写入，并在提交期间暂停该 owner 的读取与 Catalog 发布。更新时先在目标目录写完整的临时 `.asset`/`.meta`，验证 ID、摘要和领域数据；保存旧文件备份及事务记录；先替换 `.meta`，最后替换 `.asset` 作为提交点。启动扫描前恢复：若新 `.asset` 已发布且与新 `.meta` 匹配则完成提交，否则从备份恢复旧文件；不能判定时保留文件并报错，不猜测删除。创建按 `.meta` 后 `.asset`；删除先隐藏 `.asset` 再清理 `.meta`；移动/重命名和复制须有相同的事务、冲突和身份规则。只拥有本次事务的临时文件可自动清理。

Editor 读路径在 owner 锁内取得一致快照；运行时与外部只读消费者校验 ID、长度和摘要，遇不匹配明确失败。两固定文件名不能向任意外部进程提供瞬时双文件原子性。共享 `FileSystem::write_binary_atomic()` 保持单文件语义；若底层不保证断电持久性，不宣称断电事务保证。

### 缩略图

`thumbnail` 与 `thumbnail_source` 不再作为资产段保存。Editor 在 `/Saved/AssetThumbnails/` 以 Asset ID、规范化描述内容签名、相关 meta 摘要、依赖内容签名及生成器版本定位 PNG；已有内存池保留。缓存缺失、损坏或过期时重新生成，写入失败不修改资产。纯描述材质仍只有 `.asset`。缓存不参与部署或源码版本管理；删除资产可异步清理其缓存，清理失败不影响删除结果。

### 迁移与文档

新建模型、贴图和材质直接调用领域成对编码器；Catalog、Editor 和 runtime 仅接受 YAML `.asset`。旧二进制资产返回格式错误，不提供 v1 转换器、迁移工具或自动读取。此前已产生的原字节备份继续保留，不由构建或 Editor 清理。同步修订 `document/index.md` 所指 Active 设计、OpenSpec 约束和部署说明。

## Risks / Tradeoffs

- yaml-cpp 接受的 YAML 语法比资产规范宽；入口须先限制结构与资源上限。
- `.meta` 是权威数据，丢失不能像缩略图一样重建；检查和错误信息必须明确区分。
- 迁移现有导入器、Material 编辑会话和运行时读取链较广，按可构建批次切换，最后统一移除旧正式入口。
