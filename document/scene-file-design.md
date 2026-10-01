# Scene 文件与 Editor 保存

## 用例与边界

项目场景保存在用户自行组织的 `project/asset/` 下，入口为 `<name>.scene`。它沿用资产描述的 UTF-8 YAML、Asset ID、schema、依赖及引用规则；`root_type` 固定为 `toy3d.SceneAssetData`。现有 `.asset` 只接收非 Scene 根类型。Scene 首期是纯描述，不产生处理后数据；预留 `<name>.scene.meta`，避免与同名 `<name>.asset` 的 `<name>.meta` 冲突。Editor 的 Content Browser 同时展示这两种入口文件。

本阶段保存 Editor 支持放置的 EmptyActor、Cube、Plane、StaticMesh、DirectionalLight、PointLight、Camera，及其 root Transform、root 间 attachment、灯光/相机属性和资源引用。未知 Actor/Component 类型、额外组件、无资源引用的外来运行时 mesh、无法绑定的材质覆盖或其他无法还原的状态均拒绝保存，不静默丢弃。游戏运行时状态、GPU 对象、HitProxy ID、撤销历史、Editor 观察相机和布局不写入 Scene。

## 分层、所有权与生命周期

`Toy3dResource` 管理 `.scene` 与 `.asset` 共同的 YAML 编解码、Catalog、发布、移动、复制、删除和事务恢复。扩展名与 `root_type` 的对应关系由资产层统一校验。`.scene` 使用独立的 `.scene.meta` 配对路径；当前 Scene schema 禁止 meta。

`Toy3dSceneAsset`（`engine/core/scene_asset/`）拥有纯数据 DTO、反射生成代码、领域校验、编码和解码。它不依赖 runtime。Scene 记录稳定 Actor/Component ID、类型、Transform、层级链接、静态网格 AssetRef 和可编辑灯光/相机值；只由领域层判定合法性。Scene Asset ID 与内部对象 ID 不等于 World 局部整数 ID。

`engine/editor/source/scene/` 拥有 Editor 会话、World 快照和装配。Editor composition root 串行调用。读取时先完成文件、schema、引用与对象验证，再装配候选；失败保留当前场景。World 的 Actor 和 Component 仍由 World/Actor 独占，Editor 不把反射解码器指向运行时对象。切换成功后清除旧选择、待决 HitProxy、命令历史及旧 Actor 映射。Scene 初始预览对象属于未保存场景。

## 线程、错误和平台

场景文件同步读写；Editor UI/World owner 线程执行快照和装配，不引入新任务队列。资产发布复用 `AssetPairStore` 的暂存/日志/恢复；保存失败保留脏状态，加载失败保留当前 World 并显示错误。Editor 文件路径通过已冻结的 FileSystem mount 访问，写入仅允许 `/Project/`；不拼接主机路径，不把导入源位置写入 Scene。Windows/macOS 使用相同 YAML/schema；本功能不引入平台 API。

## 验证与迁移

当前 `SceneAssetData` / `SceneActorData` schema 4 固定使用厘米。位置、相机裁剪面、局部光范围和方向光阴影距离均以厘米持久化；scale、rotation、bias 与 fade fraction 保持无量纲语义。旧米制 schema 不自动读取或迁移；已有项目数据只在离线的一次性转换中保持 identity 并缩放长度字段。

验证 `.asset` 原行为、`.scene` 读写及重启扫描、同 stem 两种入口、错配 root type、非法或缺失引用、事务恢复，以及 Editor 场景往返。此前没有生产 Scene 文件，因此无需自动迁移；反射测试 fixture 不是正式格式。完成端到端往返后才启用菜单入口；旧 `.asset` 不改名。
