# 编辑器资源接入计划（Draft）

## 1. 当前基础与规范入口

通用反射、值编解码、Asset 外层、索引和编辑事务已经建立。原 `engine/resource` 收敛到 `engine/core/asset`，保留 `Toy3dResource` target 和现有文件格式。正式静态模型链路遵循 [StaticMesh 导入与资产加载](static-mesh-import-design.md)；基础 contract 见 [资源基础设计](editor-resource-foundation-design.md)，资源根与部署见 [资源目录设计](resource-directory-design.md)。本文只规划剩余接入工作，不定义第二套模型格式。

Editor 已有 Dockspace、Place Actors、Content Browser、Outliner、Details、Actor HitProxy、ImGuizmo 和统一命令历史。当前 World 是临时编辑场景，没有场景文件保存。Editor 的创作 FileSystem 用可写 `/Project` 与只读 `/Engine`；Runtime 用部署目录的只读 mount，二者共用同一个实现。

## 2. 目录与依赖

```text
engine/core/
  asset/                 通用文件、身份、索引、编辑事务
  mesh_description/      位置与逐角属性、源网格 codec
  static_mesh/           正式 DTO、render geometry codec、Asset 读取
  reflection/
  serialization/
engine/tools/
  asset_import/          Assimp 私有适配与候选产物
  mesh_builder/          纯 CPU MeshDescription → 几何
  model_import/          CLI 与测试
engine/editor/source/
  asset_tools/           项目发布策略、刷新 catalog
  placement/             ActorFactory 与对象放置
  panels/                UI
engine/runtime/
  rendercore/geometry/   StaticMesh 与 Asset 适配
  renderscene/geometry/  StaticMeshRenderData
engine/asset/            引擎内容
project/asset/           游戏内容，子目录由用户安排
```

不再建立笼统 resource 层。工具只依赖 Core 与第三方，Runtime 不链接 Assimp。后续确有独立资产创建职责时再拆 `editor/source/factories`；不为未来功能预建空目录。场景放置的 ActorFactory 与资产文件创建 Factory 是不同职责。

## 3. 静态模型首版使用

Windows `build_win.bat Debug`、macOS `build_macos.sh Debug` 开启 Assimp 导入；手动 CMake 需要 `-DTOY3D_ENABLE_ASSIMP_MODEL_IMPORT=ON`。关闭该选项仍能加载现有 StaticMesh Asset，Editor 隐藏导入菜单。

1. 启动 `bin/Toy3dEditor.exe`，在 Content Browser 选择目标项目文件夹。
2. Content Browser 空白处右键 `Import...` 或外部模型文件拖入，共用导入确认框；也可从 `File → Import Static Mesh...` 触发。选择 FBX/OBJ/glTF/GLB，确认资产名和 scale。创建与导入已收敛到菜单，不再提供 Content Browser 工具栏 Import 按钮。
3. 点击导入后合并静态 mesh 实例，原子创建 `.asset`，不覆盖同名文件；刷新、选中并生成缩略图。
4. 将 StaticMesh 图块拖入 Scene Viewport 创建 Actor，可选择、Gizmo 编辑、删除与 undo/redo。
5. 重启后从 Content Browser 重新选中资产并放置；加载不需要原 FBX。场景 Actor 本身目前不保存。

当前仅一个 LOD、UV0、顶点色、材质槽；默认材质用于预览，源材质/纹理/相机/灯光不生成引擎资产。拒绝蒙皮与 morph。资产原点保留，节点变换烘焙；无自动居中或缩放到视口。导入当前同步执行，大模型会阻塞 UI。

CLI 在构建输出目录提供 `Toy3dModelImport`：

```text
Toy3dModelImport.exe source.fbx new-file.asset
```

输出父目录须已存在。CLI 与 Editor 调用同一导入/构建/编码实现，首次创建使用 `CreateNew`。Editor 返回日志和错误窗口；磁盘保存后 catalog 刷新失败会明确指出已发布路径，不删除已成功保存的文件。

## 4. 宏、身份与所有权

`WITH_EDITOR` 隔离编辑行为；`WITH_EDITORONLY_DATA` 隔离导入数据类型和源段编码。Runtime DTO 不因宏改变布局；runtime 只消费 `type_data` 与必需 `render_geometry`，不解码可选 `source_mesh`、`import_data`。`Edit` 属性不表示 Editor 专用字段。

`AssetId::try_generate` 位于 Core Asset，使用随机 128 位非零身份；失败保留输出，没有全局 RNG，调用方查重。它不是加密身份或内容 hash。当前 Windows/macOS 采用 C++ 标准库 random_device；熵源异常或全零输出失败，不用时间或路径兜底。以后重导入必须保持既有 ID，而不是生成替代 ID。

每次源导入独占 importer、源 mount 和候选，不建新线程池。Editor 在 Game Thread 发布；catalog 保留最后有效结果。Actor history 保存 Asset ID 与 CPU prototype，重建时创建新的 StaticMesh/RenderData；释放顺序沿现有注销 → render command drain → 资源释放。

## 5. 下一批工作

| 批次 | 内容 | 前置约束 |
| --- | --- | --- |
| 模型体验 | 资产拖入视口、按 bounds 聚焦、只读网格详情 | 共用 Actor 放置/history，不能让面板直接写 RenderProxy |
| 重导入 | source locator、稳定 material slot/source key、设置与源变更 | 当前只保存源文件名；不能宣称已有自动重导入能力。保留 Asset ID，完整候选成功后原子替换 |
| 类型化编辑 | 正式领域 schema、EditSession、脏状态和关闭提示 | blob 与 type_data 同 owner 发布，防止旧 blob 覆盖重新导入结果；未知内容保持只读 |
| 材质资产（当前下一步） | Phong/Unlit、代码参数、Material/单层 Instance、贴图、槽位赋值、预览和手动重编译 | 遵循已确认的[材质设计与 M1～M7 清单](material-system-design.md)，Properties 是权威 schema，保留 orphan，不按 native slot 绑定 |
| 场景保存 | Actor/Component 身份、层级、资源引用与 staging | 先设计受控装配与生命周期失败回滚，不绕过 World 创建入口 |
| 动画 | 骨架、轨道、key、事件、压缩产物 | 分离源数据与运行时数据，先建立动画领域对象 |
| 碰撞 | shape、mesh 引用和物理产物 | 领域验证后由物理适配构造，通用 Asset 不依赖物理后端 |

资产共有文件外层与身份，不把各领域 DTO 或业务算法堆入 `core/asset`。Cook、异步导入、文件监听、资源缓存和热重载分别形成可验证的后续方案。
