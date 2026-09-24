# Proposal

## Why

Toy3d 目前只有运行时 Mesh、Texture、Material 和 GameScene 对象，缺少编辑器管理资源所需的稳定类型描述、资源身份、可编辑文档与版本化读写边界。若以材质属性面板或直接遍历 C++ 对象为起点，后续模型、动画、物理碰撞和场景的导入产物、复合结构及引用关系将迫使这些接口重做。

## What Changes

- 建立 opt-in 的共享反射 schema：稳定类型/字段身份、编辑与持久化用途、值/容器/受控多态描述及错误返回；显式标记的创作数据属性默认进入对应 Asset 文件的类型数据段，`Transient` 显式排除。由构建工具为创作数据生成登记和读写代码，避免逐字段手写访问 lambda。
- 建立 Asset 与子资源的稳定身份、类型化引用和解析边界；源文件路径、Asset ID、场景对象 ID 和运行时资源指针承担不同职责。
- 与反射同步建立 Toy3d 通用 Asset 文件格式及其序列化和反序列化闭环：统一文件头、类型与版本、依赖/子资源索引、分段边界和原子保存；具体数据段仍由模型、动画、碰撞、场景各自的类型编解码器负责。外部格式解析和转换属于后续领域导入器，不由属性标记触发。
- 建立供未来 Editor 使用的属性读取、验证、修改事务、撤销所需数据及预览通知 contract；领域 setter 或重建流程继续决定运行时对象怎样生效。
- 用模型、动画、物理碰撞和场景四种代表性文档验证 schema 能表达大数组/二进制引用、轨道、形状变体、对象图和资源依赖；材质参数面板必须复用现有 Shader `Properties` schema，不建立第二套参数身份。
- 本 change 交付反射、Toy3d Asset 文件格式与创作数据序列化/反序列化的基础 contract、生成/编解码闭环和跨资源类别的代表性验证，不交付完整模型/动画/物理导入器、Cook、场景编辑器界面或通用 UObject/GC 系统。完整资源生产路径分别由后续 change 接入。

预期调用链如下；参数、所有权和失败语义见 [design.md](./design.md)，后续 tasks 必须以此调用链为验收依据：

```cpp
register_generated_fixture_types(type_registry); // 生产模块改用各自生成的注册函数。
type_registry.freeze();
auto summary = inspect_asset(file_system, path, file_limits); // 只读通用索引。
ModelAssetData asset{};
auto loaded = load_asset(type_registry, schema_migrations, file_system, path,
                         "toy3d.ModelAssetData", asset, validate_model,
                         file_limits, value_limits);
EditSession<ModelAssetData> edit_session(type_registry, *model_type,
    summary.value().asset_id, path, asset, validate_model, &asset_index);
auto bound = edit_session.bind_published(file_system);
auto edit = edit_session.apply_edit({EditPatch{property_path, encoded_value,
                                              EditChangeKind::Setter}});
auto saved = edit_session.save(file_system, schema_migrations, summary.value(), blob_segments);
// 资源领域解析引用并构造运行时对象；场景对象由 GameScene 专用入口装配。
```

## Capabilities

### New Capabilities

- `editor-resource-foundation/reflected-schema`: opt-in 类型与字段描述、生成器、支持的数据形态、注册生命周期和错误语义。
- `editor-resource-foundation/asset-identity`: Asset/子资源身份、类型化引用、路径映射与缺失/冲突错误。
- `editor-resource-foundation/resource-document`: 通用 Asset 文件容器、类型数据段、版本迁移、依赖、读写边界和可靠保存。
- `editor-resource-foundation/editor-operations`: 属性编辑事务、验证、撤销数据、脏状态及预览通知的无界面 contract。
- `editor-resource-foundation/resource-kind-contracts`: 模型、动画、碰撞和场景的代表性数据形态，以及到领域运行时对象的构造边界。

### Modified Capabilities

无。本 change 不改变现有 `game-render-framework` 的 Mesh、Texture、MaterialRenderProxy、RHI 或 RenderCommand 行为；后续资源加载接入这些对象时再修改相应 capability。

## Impact

- `engine/asset/` 是与 UE `Content` 对应的资产目录，保留现有名称；资源可按项目需要自由组织子目录，类型由 Asset 文件头识别，不由目录名推断。共享代码分别放入 `engine/core/`、`engine/resource/` 和 `engine/tools/`，不写入资产目录；生成文件只进入构建目录。具体目录与目标名在 design 中固定。
- 本 change 不建立独立的诊断系统。底层返回错误状态和必要上下文，调用方使用现有日志记录；Editor 在需要用户处理时显示错误 Dialog。
- 复用 `Toy3dFileSystem` 的 `VirtualPath` 与原子写入能力；Asset 解析不下沉到文件系统。Editor 修改可复用 Core Math 值类型，运行时 `StaticMeshRef`、`TextureRef`、`MaterialInstanceRef` 不成为磁盘身份。
- 将文件系统中重复的 UTF-8 有效性检查收敛到 `Toy3dText`，值编解码复用它；文件路径规则仍留在 FileSystem。
- 首批不让通用反射构造 `Actor`/`ActorComponent` 或直接写有生命周期副作用的字段；场景装配仍由 GameScene 专用入口负责。正式接入场景加载时需同步修订 `document/gamescene-design.md` 中 G1 阶段的非目标说明；本 change 不改变现有 World 生命周期。
- 设计文档须给出可供 tasks 引用的 C++17 API 伪代码、四类资源数据示例、线程/所有权、错误、平台构建、迁移和验证矩阵，并同步登记 `document/index.md` 与 Core 使用索引。
