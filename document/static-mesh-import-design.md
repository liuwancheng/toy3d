# StaticMesh 导入与资产加载

## 1. 目标与边界

本方案在收敛后的 Core Asset 基础上实现 FBX/OBJ 静态网格生产链：源文件 → MeshDescription → MeshBuilder → 单文件 `.asset` → runtime StaticMesh → Editor Actor。术语参考 UE 的 MeshDescription、MeshBuilder、AssetTools 与 Factory，保持 Toy3d 的值类型、显式所有权与现有 World 生命周期。

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

根类型 `toy3d.StaticMeshAssetData`、schema 1，由现有 ReflectionCodegen 显式生成注册与 codec，保存槽位名、顶点/索引数量及几何段名。必需 `render_geometry` blob 有独立版本与显式 little-endian 定宽字段；可选 `source_mesh` blob 保存归一化后的 MeshDescription；可选 `import_data` 保存源文件名与导入设置。Editor 源数据声明使用 `WITH_EDITORONLY_DATA` 隔离，runtime 不解码源段。无通用 AssetDocument，不写 C++ 内存布局。

只有全部候选、源数据与几何编码成功后才能 `write_binary_atomic(CreateNew)`。现有 `save_asset` 用于更新已发布资产，首次创建不调用它。重名拒绝覆盖，索引刷新失败保留旧 catalog 并明确报告已发布路径；不谎称跨磁盘与索引事务。

## 4. 坐标与质量

Importer 还写入可选 `thumbnail_source` 内容签名。Editor 在导入成功后独立生成并保存缩略图，图片失败不撤销模型；包内 PNG、源签名失效和写回规范见 [Asset 缩略图](asset-thumbnail-design.md)。`encode_static_mesh_asset()` 重建几何会移除旧图片/签名；`decode_static_mesh_asset(bytes)` 从完整只读快照解码并允许未知可选外层段，既有 `read_static_mesh_asset()` 复用该入口。

输出固定 LH、+X right、+Y up、+Z forward、米、CCW。Assimp FBX 的自动 root 轴向修正会带 UnitScaleFactor，因此明确禁用该项，并使用 FBX metadata 的 Coord/Up/Front axis 与 sign 组成转换，UnitScaleFactor × 0.01 仅应用一次；不再执行 GlobalScale/MakeLeftHanded。OBJ/glTF 使用 RH Y-up 假设与 Z 反射；OBJ 单位默认米，额外用户 scale 显式保存。

层级变换合成后转换位置，法线使用 inverse transpose；负 determinant 反转三角形绕序。奇异/非有限变换失败。缺失法线生成逐面法线；缺失 UV0 置零并提示；缺失顶点色置白。退化面、非法索引与非有限数据拒绝发布。源 slot 名重复时附加源 material index，保留可区分身份。

## 5. API 与生命周期

```cpp
auto imported = import_static_meshes(source_files, source_path, options);
// 检查结果，逐项记录 warnings。
auto built = build_static_mesh(imported.value().front().mesh);
auto encoded = encode_static_mesh_asset(id, metadata, built.value(), editor_segments);
auto published = project_files.write_binary_atomic(destination, encoded.value(), FilePublishMode::CreateNew);
// 重启后：
auto geometry = read_static_mesh_asset(project_files, destination);
auto mesh = create_static_mesh_from_asset(geometry.value(), default_material);
// 由现有 ActorFactory/命令历史放置；Gizmo 和 HitProxy 继续走原链路。
```

每一步检查返回值。注册表由调用方显式创建、注册并冻结；读取建立完整候选后返回。文件系统、importer 与临时候选归单次调用独占；没有新全局服务或线程池。Editor 在 Game Thread 串行导入，完成后更新 catalog；大文件会阻塞 UI，后续组合 TaskGraph。每次 Actor 创建使用新的 StaticMesh 渲染资源生命周期，history 持有 CPU prototype，删除后不能复用已经释放的 GPU 上传对象。

## 6. 错误、平台与验证

错误复用 AssetStatus/ValueStatus/FileStatus；底层返回，CLI/Editor 使用现有日志和 Dialog，不新增诊断系统。外部文件通过只读 source mount 和 Assimp IOSystem 读取，旁文件只能在该根内访问；拒绝绝对宿主路径、越根路径与 symlink。读取与产物限制不保证 Assimp 内部解析峰值内存上限，只面向开发者选定的本地资产。

Windows/macOS 共用格式与转换；macOS 没有实机验证时如实说明。验证要求覆盖 FBX/OBJ/glTF 样本、节点与轴/单位、镜像绕序、UV 接缝、材质 section、退化/损坏/超限数据、确定性编码、首次原子创建/重名拒绝、移走源文件后加载和 Actor undo/redo。目录收敛先完成配置与旧测试，再接新链路；删除旧 `engine/resource`，无格式迁移、无双入口。旧 tinyobjloader 的其他用途不在本次删除范围。
