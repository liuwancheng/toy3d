## Why

当前 `.asset` 是包含描述、处理后数据和缩略图的二进制容器。属性无法直接审阅或合并；修改缩略图也会重写整份资产。用户已确定以同名 `.asset` YAML 描述和 `.meta` 处理后数据作为创作格式；纯描述资产只保留 `.asset`，导入时不保存原始 FBX/PNG。

## What Changes

- 将 `.asset` 改为有版本、稳定身份、类型、依赖、子资源和类型属性的有界 YAML 文档。资产引用继续使用 Asset ID 和可选 Subresource ID。
- 模型几何和贴图 mip 等必须保留的处理后字节写入同名 `.meta`；描述文件记录 `.meta` 的版本、大小及摘要。纯描述资产不得要求 `.meta`。
- Editor 只展示 `.asset`，创建、保存、重命名、移动、复制及删除按资产整体处理；缺失、孤立或不匹配的文件须诊断。
- 缩略图移入 `/Saved/AssetThumbnails/` 可再生成缓存，不参与资产发布、版本管理或运行时加载。
- 资产层实现成对发布和启动恢复。共享文件系统仍只提供单文件原子操作。
- 新建与加载直接使用 YAML 描述和可选 meta；旧二进制 `.asset` 明确拒绝，不提供转换器或兼容读取。

## Capabilities

### New Capabilities

- `asset-pair-storage`: 规定 YAML 描述、可选二进制 `.meta`、资产整体操作、故障恢复与迁移行为。

### Modified Capabilities

- `editor-resource-foundation/resource-document`、`editor-resource-foundation/asset-identity`、`editor-resource-foundation/editor-operations`：实施时同步修订当前 Active 设计与原 change 中关于单文件二进制容器的约束。

## Impact

- `Toy3dResource`、反射生成编解码、StaticMesh/Texture2D/Material 领域编解码与 importer、EditorWorkspace/Content Browser/缩略图池、runtime asset loader、部署和测试。
- `.meta` 是不可丢弃的处理后数据；缩略图缓存可以删除并重建。没有原始导入文件时，重新导入必须由用户重新提供源文件。
- 当前尚无跨进程多文件瞬时原子性承诺；Editor 内串行发布、重启恢复及读取校验构成一致性边界。
