## 1. 格式与基础验证

- [ ] 1.1 固定 YAML 键、值类型、排序、引号、换行、版本和大小上限；准备材质、模型、贴图 golden fixtures 与损坏输入。
- [x] 1.2 在 `Toy3dResource` 接入 yaml-cpp，实现严格的描述读写与确定性输出，不把 YAML 类型暴露到领域公共接口。
- [ ] 1.3 实现有界 `.meta` 段容器、Asset ID/长度/摘要校验和按需段读取；覆盖截断、重叠、重复及未知必需段。
- [ ] 1.4 将通用检查、加载与保存入口统一到新格式；验证失败不改变候选或脏状态，拒绝旧二进制 Asset。

## 2. 发布、恢复与索引

- [x] 2.1 盘点并补齐共享 FileSystem 所需的现有 rename/replace/remove 原语；事务规则留在 `Toy3dResource`。
- [ ] 2.2 实现资产 owner 串行发布、暂存、备份、事务记录、描述提交点及启动恢复；故障注入覆盖各写入/替换步骤。
- [ ] 2.3 Catalog 只显示 `.asset`，核对必需 `.meta`、孤立 `.meta`、重复 ID、同名/大小写冲突和强依赖，失败保留最后有效索引。
- [ ] 2.4 从 Catalog 构建反向引用视图，为删除和替换提供引用者诊断。

## 3. 领域与运行时接入

- [x] 3.1 StaticMesh importer 输出 YAML 描述与 `render_geometry` meta；不写 `source_mesh`，重导入要求重新选择源文件。
- [x] 3.2 Texture2D importer 输出 YAML 描述与 `texture_mips` meta；不保存原始 PNG/JPEG。
- [x] 3.3 Material/MaterialInstance 使用纯 `.asset`；保存、撤销、依赖更新和冲突检测均以新描述为准。
- [x] 3.4 runtime StaticMesh/Texture2D/Material loader 从新格式读取；模型/贴图缺 meta 明确失败，纯描述材质不访问 meta。

## 4. Editor 资产操作与缩略图

- [ ] 4.1 收敛创建、保存、重命名、移动、复制、删除到资产整体操作；复制分配新 Asset ID，移动保持原 ID；处理只读 `/Engine`、跨 store 和重名冲突。
- [ ] 4.2 Content Browser 仅显示 `.asset` 并呈现缺失/孤立 meta、引用者和操作失败诊断；不显示事务临时文件。
- [x] 4.3 将缩略图和源签名移到 `/Saved/AssetThumbnails/`；按内容和生成器版本失效，可删除重建，更新失败不改资产。

## 5. 格式收敛、文档与验收

- [x] 5.1 删除 v1 转换器及迁移工具；新建模型、贴图和材质直接编码新格式，原字节备份保留在资产目录外。
- [x] 5.2 生产 Catalog、Editor 和 runtime 拒绝旧二进制 `.asset`；验证工作区包含旧文件时保持上次有效索引。
- [x] 5.3 同步 `document/index.md` 的 Active 资源设计、StaticMesh、Material、缩略图、资源目录文档及旧 OpenSpec 约束。
- [ ] 5.4 构建受影响 targets，运行格式、事务故障注入、Catalog、导入、材质、缩略图和 runtime 回归；由独立 sub-agent 按 `verify-toy3d-build` 复核。
