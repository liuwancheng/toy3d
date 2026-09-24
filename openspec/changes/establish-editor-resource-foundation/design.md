# Design

## Context

动机见 [proposal.md](./proposal.md)。现有 `Toy3dFileSystem` 提供 `VirtualPath`、可检查的 I/O 结果和单文件原子写入；`Toy3dMath` 提供稳定的 `Transform`/向量/Quaternion 值。`StaticMesh`、`Texture`、`Material` 是运行时对象，持有或关联 RenderResource；`World`/`Actor` 独占对象并在创建时触发生命周期。Shader `.shader Properties` 已定义 Material 参数权威 schema。本设计不得把这些边界合并为一个可遍历的进程内对象图。

相关行为 contract 分见本 change 的五份 delta specs。以下 C++17 片段是目标 API 伪代码，名称和参数可在实现中微调，但所有权、错误和调用顺序必须保持。

## Goals / Non-Goals

**Goals:**

- 同一份创作数据 schema 支持生成编解码、无界面属性检查及后续 Editor 检查器；数据声明不需要逐字段 lambda。
- 给模型、动画、碰撞、场景定义足够宽的值/引用/大块产物边界，并通过代表性 fixture 验证。
- Asset 文件读取、类型化数据编辑与保存均有完整候选与明确失败结果；运行时对象继续由领域模块构造。

**Non-Goals:**

- 不引入 UObject、GC、任意方法调用反射、运行时动态类定义或全局可变 service locator。
- 不在本 change 提供完整 glTF/FBX/动画/物理导入器、Cook、Editor UI、场景运行时装配或 RHI 资源热重载。
- 不使反射 schema 取代 Shader Properties、Shader reflection、Asset import 配方或 RenderCore typed shader parameters。

## Decisions

### 1. 模块与依赖方向

```text
engine/tools/reflection_codegen/  → 构建目录中的 generated C++
engine/core/reflection/          → Toy3dReflection，纯类型/属性描述
engine/core/text/                → Toy3dText，共享 UTF-8 有效性检查
engine/core/serialization/       → Toy3dSerialization，值编解码与错误返回
engine/resource/                 → Toy3dResource，Asset 身份、统一文件容器、索引、引用与编辑事务
engine/asset/                    → 资产文件及用户自定子目录，不放系统 C++ 代码
runtime / editor / tools         → 按需依赖上述目标；Toy3dResource 不依赖 runtime
```

`Toy3dReflection`、`Toy3dText` 与 `Toy3dSerialization` 是跨模块基础设施，独立于 Asset；`Toy3dResource` 是第一方共享资源领域目标，不依赖 RenderCore/GameScene/Editor。文件系统现有多个本地 UTF-8 校验实现迁移到 `Toy3dText`，Serialization 复用同一入口，路径额外的 NUL、segment 等策略仍由 FileSystem 检查。`engine/asset/` 沿用现有资产目录，语义对应 UE 的 `Content`，可按项目需要任意分层；目录名不是资源类型或持久化身份。具体领域 DTO 与加载适配器留在各领域目标，资源基础目标只解释通用文件外层。现有 CMake 复制目录和 runtime mount 仍使用 `asset` 名称，本 change 不搬动既有资产。所有新目标使用 C++17、`target_*` CMake 配置，生成头文件只进入构建目录；构建不隐式下载生成器或编解码依赖。错误处理沿用结果返回、现有日志和调用方 UI，不新增诊断目标。此依赖方向允许离线 Cook 和 Editor 复用资源基础能力。

下文按已实现的模块局部 `ReflectionStatus`、`ValueStatus`、`AssetStatus` 与 `AssetResult<T>` 表达成功或失败，不引入全局 Result/诊断框架。资源层失败返回错误码、简短原因及可用的资源 ID、虚拟路径或字段位置；文件 I/O 错误保留现有 `FileStatus` 信息。底层不自行弹窗，也不在每层重复记录同一错误；runtime/tools 的调用方使用现有 Logger 记录，Editor 调用方在需要用户操作时显示 Dialog，并可同时记录日志。

### 2. 声明式生成与运行时描述

仅对创作数据采用 opt-in 的类型/字段标记；生成器读编译配置中的明确头文件列表，输出稳定的类型描述与编解码函数，且失败时报告源文件、行、类型、字段。标记语法以跨 MSVC/Clang 的最小原型定稿；下列调用形态是验收目标，不是已存在的宏：

```cpp
TOY3D_REFLECT_TYPE("toy3d.AnimationAsset", 1)
struct AnimationAssetData
{
    TOY3D_PROPERTY("skeleton", Edit, AssetType("toy3d.SkeletonAsset"))
    AssetRef skeleton;

    TOY3D_PROPERTY("tracks", Edit, Category("Animation"))
    std::vector<AnimationTrackData> tracks;

    TOY3D_PROPERTY("duration_seconds", Visible | Transient, Category("Animation"))
    float duration_seconds = 0.0f;
};

ReflectionStatus register_content_types(TypeRegistry& registry)
{
    return register_generated_fixture_types(registry); // 当前 fixture；生产模块使用各自生成函数。
}
```

初版用途标记限定为 `Edit`、`Visible`、`Transient`：声明了 `TOY3D_PROPERTY` 的创作数据字段默认参与 Toy3d Asset 文件中对应类型数据段的读写；`Edit` 允许编辑器修改，`Visible` 仅供只读展示，`Transient` 排除文件读写。`Edit` 与 `Visible` 互斥；首版资源编辑禁止 `Edit | Transient`，避免不可保存的修改混入撤销和脏状态。未声明的 C++ 字段完全不进入资源反射。单独声明而不带用途标记的属性可以作为隐藏的持久化字段。`Category`、`Range`、`Unit`、`AssetType` 是可选显示/输入提示，不属于用途标记，也不能代替类型和领域校验。默认读写仅适用于这里定义的创作数据 DTO，不意味着直接遍历或保存运行时对象。

生成器对非法标记组合、同名异型、未登记元素、原始指针和无法访问的字段报错；生成结果不得经 `void*` 绕过领域 setter 写运行时对象。初期支持标量、UTF-8 字符串、稳定枚举、Core Math 值、嵌套结构、数组、`AssetRef` 和有限的 tagged variant；其余形态先返回不支持。选择生成代码而非 Godot 式逐字段手写登记，是为了保持大量资源类型的声明位置与字段同步；选择受限 opt-in 而非 Piccolo 式全字段扫描，是为了避免运行时缓存和所有权泄漏。

```cpp
struct PropertyDesc
{
    std::string name;
    std::string cpp_type;
    std::uint32_t usage = 0;
    EditorHint hint;
    ValueTypeDesc value_type;
    // 生成编解码按稳定字段名访问创作数据；不保存裸字段偏移作为磁盘 ABI。
};

class TypeRegistry
{
public:
    ReflectionStatus add(TypeDesc description);
    ReflectionStatus freeze();
    const TypeDesc* find(const std::string& persistent_name) const;
};
```

composition root 拥有 registry，模块明确注册后 freeze；冻结前禁止服务查询，冻结后只读且描述寿命覆盖所有资源/编辑会话。不同 executable 只注册其可用模块，缺失类型返回错误。字段名和类型名是文件身份；内部哈希只作加速，冲突必须按原始名称验证。

### 3. Asset 身份、统一文件格式和类型数据

Asset ID 采用独立于路径的持久化不透明值；子资源 ID 独立于数组顺序。资源索引由 `Toy3dResource` 的 owner 持有，维护 ID→`VirtualPath`/类型；文件移动更新映射，不改引用。`engine/asset/` 下的相对目录由使用者选择，扫描时从文件头识别类型，不从 `mesh/`、`animation/` 等目录名推断。导入器负责源对象到子资源 ID 的持续映射；无法匹配时返回明确错误，不能仅以 glTF 节点下标构造永久身份。

```cpp
struct AssetRef
{
    AssetId asset_id;
    SubresourceId subresource_id; // 空值表示顶级资源。
    std::string expected_type;
    AssetRefStrength strength = AssetRefStrength::Strong;
};

struct AssetFileIndex
{
    AssetId asset_id;
    std::string root_type;
    std::uint32_t schema_version;
    std::vector<AssetRef> dependencies;
    std::vector<AssetSubresource> subresources;
    std::vector<AssetSegment> segments; // 名称、kind、必需性、绝对偏移与长度。
};

AssetResult<AssetFileIndex> inspect_asset(const FileSystem& files,
    const VirtualPath& path, AssetFileLimits limits = {});
AssetStatus load_asset(const TypeRegistry& types,
    const SchemaMigrationRegistry& schemas, const FileSystem& files,
    const VirtualPath& path, const std::string& expected_type,
    ModelAssetData& output, ModelValidator validate,
    AssetFileLimits file_limits = {}, ValueLimits value_limits = {});
// 旧文件格式读取时在 schemas 前显式增加 AssetFormatMigrationRegistry。
AssetStatus save_asset(const TypeRegistry& types,
    const SchemaMigrationRegistry& schemas, FileSystem& files,
    const VirtualPath& path, AssetFileIndex index,
    const ModelAssetData& data, ModelValidator validate,
    std::vector<AssetSegmentData> extra = {});
```

v1 使用统一 Asset 文件外层：固定 magic 与格式版本、Asset ID、根类型、schema 版本、依赖/子资源索引及分段目录。文件可以包含不同类型的创作数据段和可选大块数据段；各段有明确 kind、偏移与长度，领域 codec 决定段内 schema。读取器先检查头、版本、偏移范围、重叠、大小上限和类型，再按 `root_type` 分派；`inspect_asset()` 只读取索引所需信息，不解码全部数据，`load_asset<T>()` 校验文件根类型与 `T` 的登记身份一致。这里借鉴 UE 包文件与 Asset Registry 的分层，但不复制 UObject 包结构，也不要求所有资源先转成一个通用 `AssetDocument` 对象。具体字节序、对齐和分段编码需在实现前用 fixture 固定，不能直接写 C++ struct 内存布局。未知可选段能原样保留才允许保存，否则资源保持只读并返回错误原因。解码先迁移和验证类型化候选，再交给调用方；写入使用现有 `FileSystem::write_binary_atomic()`。跨文件 Cook 产物由领域定义 publication 规则。

`Toy3dSerialization` 与反射在本 change 同步落地，负责已登记创作属性的值编解码和错误返回；`Toy3dResource` 负责通用文件外层、类型分派、版本迁移、引用索引及保存事务。外部 glTF/FBX 等文件由后续领域导入器解析并转换为 Toy3d 类型数据与派生产物；导入器是否运行由资源工作流决定，属性用途标记不会触发导入。派生网格、压缩动画和碰撞加速数据使用各自的有界段编码或外部 blob 引用，不依赖通用属性编解码逐字段写入。

### 4. 编辑事务与领域提交

编辑会话持有对应资源的类型化创作数据快照、撤销记录与脏状态，不持有 Renderer 或 RHI，也不复制一份通用资源对象图。属性路径中的可重排作者列表使用稳定元素 ID；值比较采用 schema 语义，不能依赖指针或序列化文本比较。事务先在候选上执行类型、引用和领域校验，成功后原子发布新快照及 old/new undo payload。预览适配器只在成功提交后接收领域化 change，并在 Game Thread 或其所有者指定线程执行。

```cpp
EditSession<ModelAssetData> session(types, *model_type, asset_id, path,
    initial_model, validate_model, &asset_index, prepare_preview, notify_preview);
AssetStatus bound = session.bind_published(files);
AssetResult<EditRecord> changed = session.apply_edit({
    EditPatch{property_path, encoded_value, EditChangeKind::Setter}});
AssetStatus undone = session.undo();
AssetStatus saved = session.save(files, schemas, file_index, blob_segments);
// prepare_preview 在候选发布前检查可行性；notify_preview 在提交后按领域处理。
```

Material 检查器对 `.shader Properties` 作只读 schema 适配并提交 `MaterialInstance` setter；不再登记一份相同参数。RenderCore 现有 FIFO/候选发布语义保持权威。导入设置改变需要重新导入；旧预览保留至完整新候选可用。

### 5. 四类资源的代表性结构与场景边界

```cpp
struct ModelAssetData {
    VirtualPath source;
    ModelImportSettings import;
    std::vector<MeshSubresourceOverride> meshes; // 每项含稳定 SubresourceId。
};
struct AnimationAssetData {
    AssetRef skeleton;
    std::vector<AnimationTrackData> tracks;       // 轨道目标用稳定身份。
    std::vector<AnimationEventData> events;
};
struct CollisionAssetData {
    std::vector<CollisionShapeData> shapes;       // Box/Capsule/ConvexMesh tagged variant。
};
struct SceneAssetData {
    std::vector<ActorRecord> actors;               // ActorId、ComponentId、类型、属性、关系。
};
```

这些 DTO 先用于生成/读写/引用/边界测试，不承诺本 change 构造完整生产资源。后续场景加载器须在未发布的候选 World 或等价 staging 中完成 ID 唯一性、附件无环、资源可解析和默认组件匹配验证；然后经 GameScene 专用创建入口按阶段构造、连边并进入生命周期。当前 `spawn_actor<T>()` 会立即注册组件，故后续 GameScene change 必须设计受控 staging/创建入口，不得让 TypeRegistry 直接 `new Actor`。失败时旧 World 保持可用。

## Risks / Trade-offs

- **生成器解析 C++ 与平台构建耦合** → 仅解析明确标记的受限语法；固定工具链、输入清单和生成输出，先做 MSVC/Clang/macOS 配置原型，构建阶段失败即停止，不回退到旧生成文件。
- **文本格式不适合大块资源** → 文档仅承载可编辑小数据与 blob 引用；Cook 格式由领域定义边界和大小校验。
- **重导入后的子资源身份可能失配** → 持久化映射、返回 orphan 覆盖的错误信息并提供人工重绑入口；禁止顺序猜测。
- **未知新字段被旧编辑器覆盖** → opaque 保留或只读失败；测试跨版本无损往返。
- **编辑与运行时预览不同步** → 事务成功与预览发布分离，失败候选保留旧预览，错误附着到对应资源版本。

## Migration Plan

1. 确认共享目标、生成器工具链/文本 codec 的离线供应及跨平台配置；新增设计入口，不修改第三方源码。
2. 以 Core Math 值、嵌套数组、受控形状变体及四类资源 fixture 验证生成描述、版本迁移、限制和确定性读写。
3. 建立 Asset ID/子资源 ID 与 `VirtualPath` 索引、类型化引用、依赖枚举和事务性编辑会话；验证移动、重导入失配及保存失败。
4. 后续独立 change 接生产导入器、Cook、Editor UI、预览适配器和 GameScene 装配。每次接入只保留一个正式入口，更新对应 Active 设计及 OpenSpec capability。

本 change 没有旧 Asset 格式需要原地迁移；已有运行时对象继续由现有创建路径使用。回滚可移除新目标及 fixture，不转换或覆盖既有资源文件。验证矩阵覆盖 Windows/macOS 生成构建、路径大小写、格式版本、数值/UTF-8/容量边界、重复身份、缺失依赖、四类资源往返、编辑 undo/redo、原子保存故障和场景图校验 fixture。
