# Asset 描述与处理数据格式

## 范围与文件

本规范是生产资产描述的共同存储约定。项目资产仍放在 `project/asset/`，引擎资产仍放在 `engine/asset/`；子目录由使用者决定。一般资产以 `.asset` 为入口，Scene 以 `.scene` 为入口；扩展名只区分 Scene，精确领域类型仍由 `root_type` 决定。Catalog、Editor 和 runtime 不读取旧二进制 `.asset`。

一般资产以 `<name>.asset`、Scene 以 `<name>.scene` 为入口。入口是 UTF-8 YAML，`format_version: 2`，含 `asset_id`、`root_type`、`schema_version`、`dependencies`、`subresources` 和 `data`。`data` 使用冻结的 Reflection 持久化字段名；引用以 Asset ID、可选 Subresource ID、预期类型与强度表达，不以路径表达。编码固定顶层键序、字段排序和换行；读取限制字节、节点、深度、数组与字符串长度，拒绝重复键、别名、自定义标签、无效 UTF-8、非有限数与未知结构。`.scene` 的 `root_type` 必须是 `toy3d.SceneAssetData`，该类型不能写入 `.asset`。

仅当资产有处理后数据时，`.asset` 旁边才有同名 `<name>.meta`。`.scene` 首期为纯描述，禁止 meta；为将来预留 `<name>.scene.meta`，避免与同 stem 的 `.asset` 争用文件。YAML 的 `meta` 节给出版本、字节数、SHA-256 和必需段名；没有 `meta` 节时其配对文件必须不存在。`.meta` 是 `TOY3DMTA` v1 二进制分段容器，记录同一 Asset ID 与有界段目录。StaticMesh 的 `render_geometry`、Texture2D 的 `texture_mips` 存于其中；Material 与 MaterialInstance 仅有 `.asset`。导入源 FBX/OBJ/PNG/JPEG、归一化 `source_mesh`、旧 `import_data` 和缩略图不存入新资产。再次导入需重新选择源文件，导入比例等设置需重新提供。

Catalog 列出 `.asset` 与 `.scene`，扫描时验证描述、扩展名与根类型、成对关系、重复 ID 和强依赖；孤立 `.meta`、缺少必需 `.meta` 或摘要不匹配均报错。复制分配新 Asset ID，移动保留 ID，跨入口扩展名移动或复制拒绝；删除前 Editor 从 Catalog 的正向依赖构建引用者检查，强引用存在时拒绝。`.meta` 与事务临时文件不作为 Content Browser 图块。

## 发布与恢复

`Toy3dResource` 的 `AssetPairStore` 拥有双文件事务策略，`Toy3dFileSystem` 仍只承诺单文件原子写入。创建或更新先写 `.asset.txn` 记录和 `.new` 候选，再保留旧文件为 `.old`，先发布 `.meta`、最后发布 `.asset`。最后一步是该 owner 的提交点。Editor 在扫描前恢复：若新描述及 meta 摘要吻合则完成清理，否则从已校验的备份回滚；无法判定时保留现场并报错。删除先隐藏 `.asset`，再移除 `.meta`；移动用 `.asset.move` 记录目标，崩溃恢复时完成或撤销目标发布。复制与原资产读取、目标创建也由资产服务处理。

Editor 创作 owner 串行调用发布接口，Worker 通过 store 读取一致的成对快照。读取结果保留本次已验证的描述原始字节，场景内容解码与保存冲突基线共用这些字节，禁止为建立基线再次读取文件。外部进程与 runtime 读取时重新校验配对和摘要，发现不一致即失败；两个固定文件名无法提供跨进程瞬时原子观察，也不宣称断电持久事务。成对操作失败不得猜测删除用户文件；只清理由本事务记录拥有且摘要相符的临时文件。

## 缩略图

StaticMesh 缩略图 PNG 放在 `/Saved/AssetThumbnails/`（Editor 的部署侧 `bin/saved/AssetThumbnails/`），按 Asset ID、模型内容签名与生成器版本命名。缓存缺失、损坏或版本过期时重新渲染；写缓存不修改 `.asset` 或 `.meta`，失败也不撤销资产。它可删除重建，不进入源码资产与部署清单。未来材质缩略图的签名还须纳入父材质、Shader 与纹理内容；当前池只实现 StaticMesh。

## 世界单位与领域版本

世界长度统一以厘米存储。SceneAssetData / SceneActorData schema 5 与 StaticMeshAssetData schema 2、render_geometry version 2 表示厘米语义；旧米制领域版本明确拒绝，不在读取端隐式换算。YAML format_version 2 与 TOY3DMTA v1 容器版本保持不变。

## 格式边界

新建模型、贴图和材质直接编码 YAML 描述与可选 meta，不生成旧二进制 Asset 包作为中间产物。旧二进制 `.asset` 遇到生产读取入口时明确失败；不提供旧格式转换工具或自动兼容路径。此前手工转换时留下的原字节备份属于用户数据，位于资产目录之外，由用户自行保管。

当前 Catalog 会读取完整 `.meta` 核验 SHA-256；大资产的增量核验和按需段读取是后续性能工作，不改变本格式与完整性规则。

## 验证边界

测试须覆盖 YAML 与 meta 往返、损坏与缺失文件、纯描述资产、成对发布/删除/复制/移动及恢复、旧二进制资产拒绝、材质保存冲突、模型与贴图运行时读取、缩略图缓存重载。Windows 构建通过不代表 macOS、移动端或 DirectX 后端已实测。
