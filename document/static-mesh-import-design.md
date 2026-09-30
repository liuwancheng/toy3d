# StaticMesh 导入与资产加载

## 1. 目标与边界

本方案在收敛后的 Core Asset 基础上实现 FBX/OBJ 静态网格生产链：源文件 → MeshDescription → MeshBuilder → `.asset` YAML 描述与 `.meta` 几何 → runtime StaticMesh → Editor Actor。术语参考 UE 的 MeshDescription、MeshBuilder、AssetTools 与 Factory，保持 Toy3d 的值类型、显式所有权与现有 World 生命周期。下文旧包内源段与缩略图描述由 [Asset 描述与处理数据格式](asset-pair-format-design.md) 覆盖。

首版合并源文件中的静态 mesh 实例，保留导出原点，将层级变换烘焙进顶点；返回候选列表，便于以后支持拆分资产。拒绝骨骼蒙皮与 morph；动画、相机与灯光不进入输出并向调用方返回提示。材质只保留源槽位名，不导入材质、纹理或创建源场景。首版仅 UV0、顶点色、一个 LOD；不提供重导入、Cook、异步任务、缓存、场景保存或自动居中。

## 2. 目录与依赖

| 位置 | target | 职责 |
| --- | --- | --- |
| `engine/core/asset` | `Toy3dResource` | 通用格式、身份、索引；迁移保留名称和 API |
| `engine/core/mesh_description` | `Toy3dMeshDescription` | 位置顶点、逐角属性、三角形、材质组与源数据 codec |
| `engine/core/static_mesh` | `Toy3dStaticMeshAsset` | 正式反射 DTO、构建几何 codec、资产读取与编码 |
| `engine/tools/asset_import` | `Toy3dAssetImport` | Assimp 私有适配、文件系统 IO、静态候选转换 |
| `engine/tools/mesh_builder` | `Toy3dMeshBuilder` | 不依赖 Assimp 的 CPU 网格构建 |
| `engine/tools/model_import` | `Toy3dModelImport` | CLI composition root；保留原 probe |
| `engine/editor/source/asset_tools` | Editor 源码 | 源文件挂载、导入、原子发布、刷新及 UI 错误 |
| `engine/runtime/rendercore/geometry` | Runtime 源码 | 资产几何转换为 StaticMeshDesc；不依赖 Assimp |

Tools 仅链接 Core 与第三方，不依赖 runtime/editor。Core 不保存 MaterialInstance、RHI 或 Actor。引擎资产 `engine/asset` 和项目资产 `project/asset` 是内容目录，内部文件夹仍由用户安排。

## 3. 数据与格式

`MeshDescription` 将位置与逐角 normal/UV/color 分开，三角形引用 corner ID 和 material slot；不按位置焊接，避免 UV 接缝与硬边丢失。首版构建每个角一个 render vertex，按材质组生成连续 section 和 UInt32 索引；暂不进行顶点优化或切线生成。

根类型 `toy3d.StaticMeshAssetData`、schema 1，由现有 ReflectionCodegen 显式生成注册与 codec，保存槽位名、顶点/索引数量及几何段名。必需 `render_geometry` 段位于同名 `.meta`，有独立版本与显式 little-endian 定宽字段。新导入不保存 `source_mesh`、`import_data` 或源文件；再次导入需重新选择源文件和设置。无通用 AssetDocument，不写 C++ 内存布局。

只有全部候选、源数据与几何编码成功后才能通过 `AssetPairStore::publish(CreateNew)` 成对发布。重名拒绝覆盖，索引刷新失败保留旧 catalog 并明确报告已发布路径；不谎称跨磁盘与索引事务。

## 4. 坐标与质量

Editor 在导入成功后独立生成缩略图，图片失败不撤销模型；PNG 存于 `/Saved/AssetThumbnails/`，缓存失效见 [Asset 缩略图](asset-thumbnail-design.md)。读取模型时先校验 `.asset`/`.meta`，再建立只读几何候选；旧二进制 Asset 不受支持。

输出固定 LH、+X right、+Y up、+Z forward、米、CCW。Assimp FBX 的自动 root 轴向修正会带 UnitScaleFactor，因此明确禁用该项，并使用 FBX metadata 的 Coord/Up/Front axis 与 sign 组成转换，UnitScaleFactor × 0.01 仅应用一次；不再执行 GlobalScale/MakeLeftHanded。OBJ/glTF 使用 RH Y-up 假设与 Z 反射；OBJ 单位默认米，额外用户 scale 在导入时应用，重新导入时需再次指定。

层级变换合成后转换位置，法线使用 inverse transpose；负 determinant 反转三角形绕序。奇异/非有限变换失败。缺失法线生成逐面法线；缺失 UV0 置零并提示；缺失顶点色置白。退化面、非法索引与非有限数据拒绝发布。源 slot 名重复时附加源 material index，保留可区分身份。

## 5. API 与生命周期

运行时 `create_static_mesh_from_asset()` 将 `material_slots` 的作者槽名传入 StaticMeshDesc，与初始默认材质槽保持一一对应。ActorFactory 克隆几何时保留槽名；Editor 材质赋值按名称解析，而不将导入时的数值下标长期保存在历史中。材质赋值只改变当前 Actor 的覆盖，不写回 StaticMesh Asset，详见[材质系统](material-system-design.md)。

```cpp
auto imported = import_static_meshes(source_files, source_path, options);
// 检查结果，逐项记录 warnings。
auto built = build_static_mesh(imported.value().front().mesh);
auto encoded = encode_static_mesh_asset_pair(types, id, built.value());
auto published = asset_pairs.publish(destination, encoded.value(), FilePublishMode::CreateNew);
// 重启后：
auto geometry = read_static_mesh_asset(project_files, destination);
auto mesh = create_static_mesh_from_asset(geometry.value(), default_material);
// 由现有 ActorFactory/命令历史放置；Gizmo 和 HitProxy 继续走原链路。
```

每一步检查返回值。注册表由调用方显式创建、注册并冻结；读取建立完整候选后返回。文件系统、importer 与临时候选归单次调用独占；没有新全局服务或线程池。Editor 在 Game Thread 串行导入，完成后更新 catalog；大文件会阻塞 UI，后续组合 TaskGraph。每次 Actor 创建使用新的 StaticMesh 渲染资源生命周期，history 持有 CPU prototype，删除后不能复用已经释放的 GPU 上传对象。

## 6. 错误、平台与验证

错误复用 AssetStatus/ValueStatus/FileStatus；底层返回，CLI/Editor 使用现有日志和 Dialog，不新增诊断系统。外部文件通过只读 source mount 和 Assimp IOSystem 读取，旁文件只能在该根内访问；拒绝绝对宿主路径、越根路径与 symlink。读取与产物限制不保证 Assimp 内部解析峰值内存上限，只面向开发者选定的本地资产。

Windows/macOS 共用格式与转换；macOS 没有实机验证时如实说明。验证要求覆盖 FBX/OBJ/glTF 样本、节点与轴/单位、镜像绕序、UV 接缝、材质 section、退化/损坏/超限数据、确定性编码、首次原子创建/重名拒绝、移走源文件后加载和 Actor undo/redo。目录收敛先完成配置与旧测试，再接新链路；删除旧 `engine/resource`，无格式迁移、无双入口。旧 tinyobjloader 的其他用途不在本次删除范围。

## 7. Editor 导入与拖放交互

Content Browser 工具栏及资源区空白处右键菜单提供 `Import...`。原生选择文件与外部文件拖入均只建立导入候选，统一打开 `StaticMeshImportDialog`，点击确认才写入项目当前目录；不允许写入 `/Engine`。设置包括源文件、资源名和统一 scale，显示合并静态实例、默认材质及不导入动画/碰撞的实际范围。批量文件最多 32 项，逐文件确认名称并串行发布；重名拒绝覆盖，成功项移出候选，失败项保留原因，取消不撤销已经明确发布的项。当前解析仍在 GT 串行执行，大文件可能阻塞 UI；后续异步化必须将 Worker 候选生产与 GT catalog 发布分开。

`source/asset_tools` 保留导入业务策略，`source/panels/static_mesh_import_dialog.*` 持有候选与 ImGui 状态；`source/platform` 只负责该模型工作流的原生选择窗口（Windows common dialog、macOS NSOpenPanel）。文件 IO 仍调用 Core FileSystem/NativePlatformFile，不建立第二套文件服务。没有引入通用 Dialog manager。

外部文件拖放属于平台窗口事件：IWindow 提供显式启用与取出 owned UTF-8 路径、客户区逻辑坐标的接口，默认关闭，非支持平台明确返回 false。单批 32 文件、每路径 4096 字节、待消费事件最多 8 个；原生回调仅复制事件并检查错误，不执行 Assimp 或修改 World。Windows 使用 WM_DROPFILES 并始终 DragFinish；macOS 使用 GLFW drop callback。Editor 在当前 Content Browser 区域接收，模态期间拒绝新批次，引擎资产目录提示只读。坐标不使用 framebuffer 像素；事件和候选均只在 GT 消费，退出时关闭接收并清空。

资源图块的 ImGui payload 仅复制 AssetId（不含 catalog 指针或宿主路径），SceneViewport 返回 `AssetPlacementRequest {asset_id, transform}`。视口 hover 只绘制落点标记；delivery 时 Editor 再从当前 catalog 校验类型/存活，读取 `.asset`，组合现有 ActorFactory 与 EditorCommandHistory。地面落点使用模型 local bounds.minimum.y 修正原点高度，未命中地面则沿视线放置。成功后选中新 Actor、取消过期 HitProxy；失败不创建 Actor、不改历史。Camera View 不接受放置，拖动期间屏蔽 Gizmo 与拾取。放置旧资源不依赖 Assimp，Engine 只读资源允许放置；顶部 `Add Selected Mesh` 入口移除。

验证覆盖设置与扩展名校验、批量上限/重名/只读目录、取消无写入、源文件失败、payload delivery 才创建、catalog 刷新后的身份解析、bounds 地面偏移、撤销重做和 Assimp OFF 加载。Windows 构建与测试独立执行；原生文件窗口和桌面外部拖放需交互验收，macOS 实机未验证时不得宣称通过。
