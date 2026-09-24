# Toy3d 编辑器资源基础设计

## 1. 状态、目标与边界

本文是 `establish-editor-resource-foundation` 的共享基础设施 contract。行为验收以该 OpenSpec change 的五份 delta spec 为准；此处固定模块、所有权、接口和实施约束。实现未完成的能力不应宣称可用。

资源基础服务于未来 Editor、runtime 和离线工具。它要让同一份创作数据声明驱动类型描述、Toy3d Asset 文件的序列化与反序列化、属性读取及编辑事务。代表性资源包括模型、动画、物理碰撞和场景，材质参数从现有 Shader `Properties` 取得权威 schema。

本阶段不提供 glTF/FBX 等外部格式的生产导入器、Cook、编辑器 UI、运行时场景装配、RHI 资源热重载、UObject/GC 或进程内通用对象图。外部文件的解析和转换由后续领域导入器负责；`Edit` 等属性标记不触发导入。

## 2. 目录、目标与依赖

| 位置 | 目标 | 职责 |
| --- | --- | --- |
| `engine/core/reflection/` | `Toy3dReflection` | 稳定类型/属性描述、用途与提示、显式注册和只读查询 |
| `engine/core/text/` | `Toy3dText` | 文件路径与值编解码共享的 UTF-8 有效性检查 |
| `engine/core/serialization/` | `Toy3dSerialization` | 有界值编解码、版本迁移所需的值接口与错误返回 |
| `engine/resource/` | `Toy3dResource` | Asset/子资源身份、统一文件外层、索引、类型化引用和编辑事务 |
| `engine/tools/reflection_codegen/` | 独立生成器目标 | 处理构建配置中的明确头文件清单，向构建目录输出 C++ |
| `engine/asset/` | 无代码目标 | 用户组织的资产文件，相当于 UE `Content`；子目录名不决定资源类型 |

依赖方向为 `Toy3dSerialization → Toy3dReflection + Toy3dText + Toy3dMath`，`Toy3dFileSystem → Toy3dText`，`Toy3dResource → Toy3dSerialization + Toy3dReflection + Toy3dFileSystem`。资源身份的值编解码在资源目标提供适配，Serialization 不反向依赖资源目标。领域 DTO 和领域校验属于对应领域目标或其测试 fixture；工具不得反向依赖 runtime，资源目标不得依赖 RenderCore、GameScene 或 Editor。生成器为构建时工具，不参与目标程序的运行时生命周期。目标统一使用 C++17、`target_*` CMake 配置；生成结果只在构建目录中，构建不隐式从网络取得工具或 codec。

`engine/asset/` 保留现有名称和文件，不强制 `mesh/`、`animation/` 等子目录。文件内根类型和 Asset ID 决定资源身份；路径只用于查找。`Toy3dFileSystem` 仍只处理字节、路径和 mount，不处理资源类型或依赖。文件系统的 `VirtualPath`、`FileStatus` 和单文件原子写入沿用 [共享文件系统设计](core-infrastructure-design.md)。

实施前盘点确认：`Toy3dFileSystem` 已有虚拟路径、受限读取、文件句柄和 `write_binary_atomic()`；`Toy3dLogging`、`Toy3dMath` 各自有独立目标；shader compiler 的 reflection 只描述 GPU shader，不是可复用的创作数据类型系统。现有 `engine/core/` 没有通用创作数据反射、值序列化或 Asset ID 目标，`engine/resource/` 尚未建立。生成器采用仅依赖 C++17 标准库的独立可执行目标，解析明确的受限标记语法；首次版本不引入 Clang SDK、运行时反射依赖或隐式网络下载。当前项目配置可在 Visual Studio 17 2022/x64、`BUILD_TESTING=ON` 下离线完成；新的目标只增加显式 `target_*` 依赖，不复用 shader compiler 的 GPU reflection 类型。

## 3. 公共接口与实现分层

以下 C++17 片段表达调用顺序和责任，具体签名允许在实现时按值类型微调：

```cpp
TypeRegistry types;
register_generated_fixture_types(types); // 示例使用生成器测试的五类资源 DTO。
ReflectionStatus ready = types.freeze(); // 失败则服务不得启动。

AssetResult<AssetFileIndex> summary = inspect_asset(files, path, file_limits);
ModelAssetData data{};
AssetStatus loaded = load_asset(types, schema_migrations, files, path,
    "toy3d.ModelAssetData", data, validate_model, file_limits, value_limits);
EditSession<ModelAssetData> session(types, *types.find("toy3d.ModelAssetData"),
    summary.value().asset_id, path, data, validate_model, &asset_index);
AssetStatus bound = session.bind_published(files);
AssetResult<EditRecord> edit = session.apply_edit({
    EditPatch{property_path, encoded_value, EditChangeKind::Setter}});
AssetStatus saved = session.save(files, schema_migrations, summary.value(), blob_segments);
```

实际调用方必须先检查注册、冻结、索引读取、类型查询、加载、绑定和编辑结果，再使用后续值；上例仅展示顺序。`ReflectionStatus`、`AssetStatus` 与 `AssetResult<T>` 是模块局部的成功或失败契约，不要求新增全项目 Result 框架。`PropertyDesc` 保存稳定字段名、值类型、用途和显示提示，不以 C++ 内存偏移作为文件 ABI。`TypeRegistry` 在显式注册并完成冲突检查后冻结；其类型/字段描述只读，供生成编解码、Editor 查询和工具共用。不同 executable 只注册自己可用的领域模块，未知类型必须失败。

`TOY3D_REFLECT_TYPE` 与 `TOY3D_PROPERTY` 只作用于 opt-in 创作数据 DTO。显式属性默认进入所属类型数据段；`Transient` 排除读写；`Edit` 可修改；`Visible` 只读；未带 `Edit`/`Visible` 的属性可保存但不进入普通检查器。`Edit | Visible` 与 `Edit | Transient` 在 v1 非法。`Category`、`Range`、`Unit`、`AssetType` 只是提示，不能替代类型、引用或领域验证。未标记字段、原始指针、RHI 句柄和运行时缓存不进入 schema。生成器报错应定位源文件、行、类型和字段。

v1 生成器只读取 CMake 明确列出的头文件，不递归展开 `#include`。标记与紧随的 `struct`、公开字段须各占一行；花括号可在声明行或下一行。反射枚举用 `TOY3D_REFLECT_ENUM("toy3d.ShapeMode")` 标记 `enum class`，每个枚举项写明确的整数值；持久化名称在 v1 使用 ASCII 标识符和点。已支持字段形态为固定宽度整数、`bool`、`float`/`double`、`std::string`、Core Math 值、同一生成清单中的已标记结构/枚举、`std::vector<T>`、有限 `std::variant<T...>` 及资源领域适配的 `AssetRef`。生成器拒绝私有/受保护属性、原始指针和未知元素类型；这些限制在扩展语法前属于公共 contract。`TOY3D_REFLECT_TYPE`、`TOY3D_REFLECT_ENUM` 和 `TOY3D_PROPERTY` 在 C++ 编译阶段展开为空，类型/属性的持久化名称只来自标记参数。

`Toy3dSerialization` 实现固定宽度值、bool、UTF-8、稳定枚举、Core Math 值、嵌套结构、受限数组与 tagged variant 的双向编解码。字段及类型身份使用持久化名称而非 C++ 声明顺序；需要改名或变型时登记明确迁移步骤。确定性输出不依赖 unordered 容器迭代顺序。未知必需字段/分支失败；未知可选内容无法原样保留时，编辑器只能只读打开，保存返回明确错误。

当前字段帧是稳定名称、单字节必需标志与长度前缀 payload；生成器按名称排序并写出必需字段。未知可选字段触发 `UnknownOptionalField`，由资源层标记只读并禁止有损保存；未知必需字段直接失败。`SchemaMigrationRegistry` 按类型稳定名及 `from_version` 登记逐版本步骤，在原始字段帧副本上显式改名或转换 payload，完整成功后才输出当前版本字节。调用方应先比较文件版本与冻结的 `TypeDesc::schema_version`，迁移完再调用生成的 `decode_value`；新版本或缺步失败。

基础 `ValueWriter`/`ValueReader` 使用显式 little-endian、IEEE 754 binary32/64、有限浮点和长度前缀 UTF-8。`ValueLimits` 同时限制总字节、字符串字节、数组元素和嵌套深度；reader 借用输入字节，调用方保持其寿命。读写失败返回 `ValueStatus`（错误码、偏移、属性路径、原因），不发布半成品字段值。资源外层再附加 Asset ID 和 `VirtualPath` 上下文。

`Toy3dResource` 解释通用 Asset 文件外层：magic、格式版本、Asset ID、根类型、schema 版本、依赖/子资源索引和分段目录。不同资源拥有各自的类型数据段及可选大块段或 blob 引用，不存在通用 `AssetDocument` 内存对象。文件字节序、对齐与有界编码必须用 fixture 固定，禁止写入 C++ struct 原始内存。`inspect_asset()` 只读取索引所需内容；`load_asset<T>()` 校验根类型并先建立完整候选；`save_asset<T>()` 经 `FileSystem::write_binary_atomic()` 发布完整文件。格式版本和类型 schema 版本分别迁移。

v1 文件固定头为 8 字节 `TOY3DAST`、little-endian `uint32` 格式版本（当前 1）、little-endian `uint32` 索引长度；随后是有界索引及独立数据段。索引依次编码 16 字节 Asset ID、长度前缀 UTF-8 根类型、`uint32` schema 版本、直接依赖列表、子资源列表和分段目录。列表长度使用 `uint32`，依赖项保存 16 字节目标 ID、16 字节可空子资源 ID、预期类型及单字节强/弱/延迟语义；子资源项保存稳定 ID 与类型。分段目录项保存名称、`uint8` kind、必需标志及 little-endian `uint64` 绝对偏移与长度；`type_data` 是必需的 kind 1，kind 2 为独立 blob。编码器按稳定名称和 ID 排序。读取时拒绝长度越界、重叠、重复名称、未知必需段和超限索引；未知可选段允许检查索引，但未经原样提供其字节不得保存。`inspect_asset()` 仅以 `read_at()` 读取固定头与索引，`load_asset<T>()` 仅按索引取 `type_data`，不展开 blob。跨宿主字节序的固定头和字段以测试字节固定。

`load_asset<T>()` 依次执行索引与根类型检查、schema 逐版本迁移、生成的类型化解码、调用方提供的领域验证，全部成功后才替换调用方值。遇旧文件格式时，显式传入的 `AssetFormatMigrationRegistry` 按格式版本对原始文件字节逐步转换，每步必须输出下一版本的有效头；随后解析新外层，再独立迁移类型 schema，原文件不会在读取阶段被改写。当前格式版本 1 无历史正式格式，测试中的版本 0 仅用于验证迁移入口，不将其定义为生产格式。当前保存入口要求已发布文件是 v1；旧格式读取后需要由迁移工具另行发布 v1 文件，不能直接由编辑会话覆盖旧格式。`save_asset<T>()` 先解码已发布的类型数据，防止未知可选字段被旧编辑器覆盖；要求调用方提供原文件中所有非类型数据段的完整字节，验证类型及领域约束，编码完整候选并调用原子写入；缺失已有大段时返回只读错误，不默默丢失。资源层返回带 Asset ID、虚拟路径、段或属性路径的错误，日志与 Dialog 仍由调用方处理。

## 4. 身份、所有权和生命周期

`AssetId` 与文件位置分离。`SubresourceId` 不由数组下标推导；`AssetRef` 持有 Asset ID、可选子资源 ID 和预期类型，不持有运行时对象。索引由 composition root 中的资源服务 owner 持有，维护 ID→`VirtualPath`/类型映射。移动资源仅更新映射；重复 ID 拒绝任意选择。导入器将源对象映射到既有子资源 ID，无法匹配时保留原引用并报告 orphan，不按新顺序猜测。

`AssetId` 与 `SubresourceId` 是非零 128 位身份，文本形式为 32 个小写十六进制字符，文件与引用值以 16 字节原样存储。`AssetIndex` 由 owner 串行修改，拒绝重复 ID 或路径，按 ID 解析根或子资源预期类型；强引用图 DFS 报告环路径，弱/延迟边不阻塞强依赖加载。`match_subresources()` 以导入器提供的稳定 `source_key` 保留既有 ID，返回新增键和 orphan 列表；后续具体模型/动画导入器负责定义 source key 的持久稳定语义、给新键分配 ID、处理 orphan 冲突和发布 Cook 产物，通用资源层不会用数组下标猜测。

composition root 先创建并冻结 `FileSystem` mount，再显式注册及冻结 `TypeRegistry`，随后构建索引和资源服务。注册表寿命覆盖加载候选、编辑会话和预览适配器；编辑会话独占其类型化创作数据快照、撤销记录和脏状态，不持有 Renderer/RHI 或 World。领域运行时对象由对应领域服务构造和销毁，通用反射不调用 `new Actor`、不绕过 setter 写运行时状态。

## 5. 线程、事务和预览

注册及冻结发生在启动线程，之后描述可并发只读。资源索引的构建和更新由其 owner 串行化；读取可基于已发布的不可变快照。v1 文件 I/O 同步；不在资源层自建线程池。编辑会话由 Editor 指定的 owner 线程修改，不能同时从多个线程写。异步导入/Cook 结果必须在 owner 线程或受控提交点验证并发布。

属性路径定位嵌套结构、数组元素和变体分支；可重排的作者集合使用稳定元素 ID。一次编辑先在候选上完成用途、类型、引用和领域不变量验证；成功才一起发布新快照、old/new 撤销数据和预览通知。复合约束允许多字段同一事务。失败不改变快照、撤销栈、脏状态或预览。成功编辑标脏；只有目标 Asset 文件原子发布成功才清脏。预览通知携带领域变更类别；领域适配器在指定线程选择 setter、重新导入/Cook 或完整候选替换。失败候选保留旧预览。

`PropertyPath` 由稳定字段名、仅表示顺序的数组下标、`identity_property + identity` 稳定元素选择器及 tagged variant 分支组成。`access_property()` 按冻结 schema 在编码的类型数据上定位字段，不借助 C++ 偏移；作者集合须在元素结构中显式保存 UTF-8 稳定 ID 字段，路径以该 ID 重定位，插入和重排后仍指向同一元素。`EditSession<T>` 独占类型化快照，`apply_edit()` 接受一个或多个 `EditPatch`；每个补丁先检查 `Edit` 用途及引用索引，最终候选由生成解码器做类型校验，再交给领域 validator 和预览准备回调。全部成功后才发布快照、old/new 字节撤销记录与通知。`EditChangeKind` 区分 `Setter`、`Reimport`、`Cook`、`ReplaceCandidate`；通用层不触碰 RHI、RenderProxy 或 Actor。undo/redo 恢复类型化快照并走相同的预览准备边界。

编辑会话固定 owner 线程，其他线程修改返回 `InvalidState`。创建后通过 `bind_published()` 捕获基线类型数据；`save()` 在写入前比较已发布类型数据，变化则返回 `Conflict` 并保留编辑和撤销记录，随后调用 `save_asset()` 原子发布；成功才更新基线并清脏。当前冲突检测针对创作数据段；领域导入器若会独立改写大块段，须在其 owner 的发布协议中额外提供相应版本检查。错误由调用方在 runtime/tools 的 Logger 或 Editor Dialog 中呈现；底层不自建日志汇聚或诊断对象，也不在多层重复记录。

场景创作数据只保存 Actor/Component 稳定 ID、类型、属性、root/attachment 与资源引用，验证唯一性、两端存在和无环。当前 GameScene 的 `spawn_actor<T>()` 会立即进入注册生命周期，因此本阶段不装配 World。未来场景加载须另行设计受控 staging/领域创建入口并同步修改 [GameScene 设计](gamescene-design.md)。材质属性以 [Shader 设计](shader-system-design.md) 的 `.shader Properties` 为权威，orphan override 不按 native slot 重新绑定。

代表性 fixture 已覆盖五类资源，而不是把这些领域 DTO 变成通用 Asset 文件结构：模型保存源 URI、导入选项、稳定节点及 mesh/material/skeleton 关联，几何顶点仍放独立 blob；后续模型导入器负责外部格式解析、稳定 source key、缺失 Cook 产物重建。动画保存骨架引用、稳定轨道目标、插值、key 与事件，领域验证时间排序、范围和目标存在；后续动画导入器负责压缩曲线和 Cook。碰撞保存 box/sphere/capsule/mesh 的有限变体与局部 Transform，领域验证尺寸和 mesh 引用，后续物理适配器负责后端形状构造。场景保存 Actor/Component ID、root、跨 Actor attachment 与资源引用，领域验证唯一性、端点和无环；本模块不调用 `spawn_actor<T>()`。材质创作数据仅保存 Shader 引用及覆盖，测试适配器直接读取 shader compiler 已解析的 `.shader Properties` 和 `ShaderParameterId` 检查类型、范围及 orphan；后续生产适配器应从已验证 Shader schema 提供只读属性视图，并遵守现有 Material replacement 与 frame safe point，不能按 native slot 猜测或直接写 RenderProxy。

## 6. 错误与安全边界

各层通过返回值传播错误码、简短原因和可用的 Asset ID、`VirtualPath`、字段路径或分段身份。I/O 错误保留 `FileStatus` 的操作和错误上下文。底层不弹 Dialog，也不重复记录同一错误；runtime/tools 调用方通过现有 Logger 记录，Editor 调用方在需用户处理时显示 Dialog，并可记录日志。不创建独立诊断框架。

读取前检查 magic、版本、索引和分段边界、偏移重叠、数据总量、数组长度、嵌套深度、UTF-8、浮点有限性、整数溢出和已登记类型。依赖强环报告路径；弱/延迟引用必须由领域 schema 明示。未知根类型或必需段拒绝消费，未知可选段无法无损保留时拒绝保存。虚拟路径必须由 `VirtualPath` 校验，不把宿主物理路径或指针持久化。构建阶段对标记输入使用明确清单，生成文件不执行任意资源代码。单文件原子发布不代表跨多个派生文件的事务，领域 Cook 需另定 publication 顺序。

## 7. 平台、迁移与验证

Windows/MSVC 和 macOS/Clang 使用相同持久化名称、文件字节序和分段 fixture；平台差异限于构建与现有文件系统后端。移动端和其他平台在公共格式层不引入宿主字节序假设；当前无生产 Cook/profile 产物时可以读取创作文件，但需要产物的预览或 runtime 请求应明确返回未就绪。

实施顺序：先建立文档和独立目标，再实现生成器/冻结 schema、值编解码、Asset 外层与身份索引，最后接编辑事务及跨类型 fixture。已有 `engine/asset/` 文件不搬动，旧 runtime Mesh/Texture/Material 和 World 创建路径继续工作。没有旧 Toy3d Asset 格式需原地转换。后续每个领域接入生产导入器或预览器时，须有新入口的同等行为测试和迁移方案，确认全部调用方迁移后才删除旧入口；Shader `Properties` 和 GameScene 生命周期入口不是本 change 的删除对象。回滚新基础目标不得改写既有资产。

| 验证层 | 主要检查 |
| --- | --- |
| 生成与注册 | Windows/macOS 构建、标记合法性、重复身份、字段重排、并发只读 |
| 值与格式 | 数值/UTF-8/容量限制、确定性字节、格式与 schema 版本、未知可选/必需内容 |
| 身份与文件 | 路径移动、大小写和重复 ID、缺失依赖、强环、短写及原子替换失败 |
| 领域 fixture | 模型子资源、动画轨道、碰撞形状、场景对象图及材质 Shader schema |
| 编辑 | 复合事务、稳定元素 ID、undo/redo、保存脏状态、失败预览保留 |

代码修改完成后的构建和测试由独立验证者运行；纯文档修改执行 OpenSpec 严格校验与本索引一致性检查。

当前验证记录：Windows Visual Studio 17 2022/x64 Debug 配置及受影响目标构建成功，完整 CTest 为 44/44，通过 OpenSpec 严格校验；反射生成文件仅位于构建目录。当前工作环境没有 macOS/Clang 或移动端构建环境，因此这些平台尚未验证。
