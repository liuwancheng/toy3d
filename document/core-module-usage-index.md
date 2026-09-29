# Core 公共模块使用索引

本文供开发者和 AI 快速定位 `engine/core/` 的共享基础设施。这里只记录入口和常用调用方式；接口、测试与专项设计文档仍是最终依据。

## 快速索引

| 能力 | CMake target | 公共入口 | 可执行示例 |
|---|---|---|---|
| 文件系统 | `Toy3dFileSystem` | `file_system/file_system.h`、`native_platform_file.h` | `engine/core/tests/file_system_tests.cpp` |
| 创作数据反射 | `Toy3dReflection` | `reflection/reflection_macros.h`、`reflection/type_registry.h` | `engine/tools/reflection_codegen/tests/codegen_tests.cpp` |
| UTF-8 校验 | `Toy3dText` | `text/utf8.h` | `engine/core/tests/text_tests.cpp` |
| 值编解码 | `Toy3dSerialization` | `serialization/value_codec.h`、`serialization/math_value_codec.h`、`serialization/schema_migration.h` | `engine/core/tests/serialization_tests.cpp` |
| Asset 容器与身份 | `Toy3dResource` | `asset_file.h`、`asset_identity.h`、`asset_index.h`、`property_path.h`、`edit_session.h` | `engine/core/asset/tests/asset_file_tests.cpp`、`engine/tools/reflection_codegen/tests/codegen_tests.cpp` |
| 源网格描述 | `Toy3dMeshDescription` | `mesh_description/mesh_description.h` | `engine/tools/model_import/tests/static_mesh_import_tests.cpp` |
| StaticMesh 资产 | `Toy3dStaticMeshAsset` | `static_mesh/static_mesh_asset.h` | `engine/tools/model_import/tests/static_mesh_import_tests.cpp` |
| 日志 | `Toy3dLogging` | `logging/logger.h` | `engine/core/logging/logger.cpp` |
| 数学 | `Toy3dMath` | `math/math.h`、`math/angle.h`、`math/transform.h`、`math/matrix_construction.h`、`math/geometry/plane.h`、`math/geometry/convex_volume.h`、`math/random.h` | `engine/core/tests/math_tests.cpp` |
| GPU-ready 格式 | `Toy3dPixelFormat` | `pixel_format/pixel_format.h` | `engine/core/tests/pixel_format_tests.cpp` |
| 内容签名 | `Toy3dHash` | `hash/sha256.h` | `engine/core/asset_thumbnail/tests/asset_thumbnail_tests.cpp` |
| 内存 PNG | `Toy3dImageCodec` | `image_codec/png_codec.h` | `engine/core/asset_thumbnail/tests/asset_thumbnail_tests.cpp` |
| Asset 缩略图格式 | `Toy3dAssetThumbnail` | `asset_thumbnail/asset_thumbnail.h` | `engine/editor/tests/thumbnail_integration_tests.cpp` |
| 线程、事件、Queue | `Toy3dThreading` | `threading/thread.h`、`event.h`、`runnable_thread.h`、`containers/queue.h` | `engine/core/tests/threading_tests.cpp`、`queue_tests.cpp` |
| Task Graph | `Toy3dTaskGraph` | `task_graph/task_graph.h`、`graph_task.h` | `engine/core/tests/task_graph_tests.cpp`、`task_graph_scheduler_tests.cpp` |

调用目标按需链接：

```cmake
target_link_libraries(MyTarget
    PRIVATE
        Toy3dFileSystem
        Toy3dTaskGraph)
```

`Toy3dTaskGraph` 会传递 `Toy3dThreading`。不要直接包含 `stalling_task_queue.h`、`bounded_mpmc_queue.h` 或继承 `BaseGraphTask`；这些是实现层入口。

## Text

`toy3d::is_valid_utf8(text)` 只检查 UTF-8 编码；NUL、路径 segment 和其他领域规则由调用方继续检查。FileSystem 的虚拟路径、物理路径和文本读取与 Serialization 共用此入口。

## Reflection

创作数据头文件显式包含 `reflection/reflection_macros.h`，在公开 `struct` 与字段前分别放置稳定名称标记；无标记字段不进入 schema。`Edit` 可编辑、`Visible` 只读、`Transient` 不保存，已标记字段无 `Transient` 时默认保存。`Category`、`Range`、`Unit`、`AssetType` 只提供编辑提示。受限声明语法、支持类型和失败边界见 `document/editor-resource-foundation-design.md`。

构建时以明确的 `--input` 清单调用 `Toy3dReflectionCodegen`，并给出构建目录中的 `--header`、`--source` 和唯一 `--function` 名；把生成 `.cpp` 加入使用目标。生成文件只放 build 目录，使用目标链接 `Toy3dReflection` 与 `Toy3dSerialization`。生成函数由 composition root 显式调用，检查返回值后冻结注册表：

```cpp
toy3d::TypeRegistry registry;
const toy3d::ReflectionStatus registered = register_generated_content_types(registry);
if (!registered.succeeded())
{
    return;
}
const toy3d::ReflectionStatus frozen = registry.freeze();
if (!frozen.succeeded())
{
    return;
}
const toy3d::TypeDesc* type = registry.find("toy3d.ModelAsset");
```

`register_generated_content_types` 是示例生成函数名，实际名称由 `--function` 决定。生成器测试演示了标记输入、生成代码编译、非法组合和未知类型拒绝。

## Serialization

`ValueWriter`/`ValueReader` 负责固定宽度值、UTF-8 文本、数组长度与嵌套深度的有界读写；每次调用都检查 `ValueStatus`。编码为显式 little-endian，不保存 C++ struct 内存布局。`ValueReader` 借用输入字节，调用方必须保持输入寿命覆盖 reader。错误通过 `ValueStatus` 携带 offset、属性路径与原因，调用方决定记录日志或显示 Dialog。资源文件外层不在此模块。

生成的 `encode_value(writer, data)` / `decode_value(reader, data)` 按稳定字段名排序。字段帧依次为名称、`uint8` 必需标志（`1` 必需、`0` 可选）及长度前缀 payload；生成器目前写出必需字段。未知必需字段拒绝，未知可选字段返回 `UnknownOptionalField`，调用方可只读展示，但不得把丢失该字段的候选保存。`SchemaMigrationRegistry` 以 `类型名 + from_version` 显式登记逐版本迁移，回调可用 `rename_schema_field` 和 `convert_schema_field` 处理字段；迁移只在完整成功后发布新字节。

## Resource

通用 Asset 代码位于 `engine/core/asset`，独立 target 名称暂保留 `Toy3dResource`。`AssetId::try_generate(output)` 生成非零随机 128 位身份，失败不修改输出；不是内容 hash，创建方仍须在 catalog 查重。正式 StaticMesh 领域类型位于 `engine/core/static_mesh`，详细格式与加载流程见 [StaticMesh 生产链](static-mesh-import-design.md)。

`AssetId::parse()` 接受非零 32 字符小写十六进制 ID；`AssetRef` 保存目标 ID、可选子资源 ID、预期类型与强/弱/延迟语义。`encode_asset_file(index, segments)` 按稳定名称生成完整 Asset 字节；`inspect_asset(files, path)` 只读取固定头和索引。`load_asset<T>(types, migrations, files, path, type_name, output, validate)` 形成完整候选并在领域验证成功后赋值；`save_asset<T>(types, migrations, files, path, index, value, validate, extra_segments)` 先检查已发布文件能无损解码，并要求提供已有大段的字节，再通过 FileSystem 原子发布。`AssetIndex` 由 composition root 持有，串行添加、移动和校验引用/强依赖环；`match_subresources()` 返回匹配、新增键与 orphan，不按数组下标重新绑定。

旧文件格式通过另一个 `load_asset<T>` 重载显式传入 `AssetFormatMigrationRegistry`，先迁移文件外层，再执行 schema 迁移；未知格式与缺失步骤返回错误且不修改原文件或调用方值。格式版本 0 目前只作迁移测试 fixture。

`access_property(types, type, encoded_value, path)` 读取嵌套字段、数组元素或变体分支；`PropertyPathPart::element_id(identity_property, identity)` 在插入和重排后按作者保存的稳定 ID 选择元素。`EditSession<T>` 在 owner 线程持有快照、撤销记录与脏状态，先 `bind_published(files)`，再用 `apply_edit({patch...})` 提交单次或复合编辑；`undo()` / `redo()` 恢复快照，`save(files, migrations, index, extra_segments)` 仅在目标文件成功原子发布后清脏。调用方提供领域 validator 与可选预览准备/通知回调，使用 `EditChangeKind` 决定 setter、重新导入、Cook 或完整候选替换；失败不发布通知。错误由 Logger 或 Editor Dialog 的调用方处理。

## Hash、PNG 与缩略图

`sha256(bytes/text)` 返回固定 32 字节签名；Shader key 的组装策略仍在 Shader，算法只留 Core 一份。`encode_png(image,bytes)` / `decode_png(bytes,image)` 是有界内存 codec，失败不替换输出，不直接操作文件或 RHI。图像为 top-left、紧凑 RGBA8，调用方负责色彩语义。

`Toy3dAssetThumbnail` 编解码可选图片/源签名段，不依赖 PNG。`replace_asset_segments(original,replacements)` 在 `Toy3dResource` 中保留其他段的原始字节、身份和引用，返回完整候选而不写文件；发布者仍须检查权限/完整文件基线并通过 FileSystem 原子发布。格式、所有权、线程和平台边界见 [Asset 缩略图](asset-thumbnail-design.md)。

## FileSystem

业务模块优先接收 composition root 注入的 `FileSystem&`，使用 `VirtualPath`；只有平台实现和 composition root 才直接使用 `NativePlatformFile`、`PhysicalPath` 和 mount 配置。所有 `FileResult<T>`、`FileStatus` 都必须检查。

```cpp
const auto parsed = toy3d::VirtualPath::parse("/shader/example.bin");
if (!parsed.succeeded())
{
    return;
}

const auto content = file_system.read_binary(parsed.value());
if (!content.succeeded())
{
    // 记录 content.status() 后返回。
    return;
}
```

mount 应在启动阶段完成并调用 `FileSystem::freeze()`；领域层不要绕过虚拟文件系统拼接宿主路径。完整边界见 `document/core-infrastructure-design.md`。

## Logging 与 Math

`Logger` 由 composition root 初始化和退出，普通调用方使用宏保留源码位置：

```cpp
TOY_LOG_INFO("Loaded {} entries", entry_count);
TOY_LOG_ERROR("Load failed: {}", error_message);
```

数学聚合入口为 `math/math.h`；编译时间敏感的调用方可直接包含 `math/angle.h`、`math/scalar_math.h` 或 `math/math_constants.h`。角度使用显式 `toy3d::Radians`、`toy3d::Degrees` 和 `to_radians()`/`to_degrees()`，裸 `float` 不隐式表达角度单位。随机数必须单独包含 `math/random.h`。

新向量代码使用 `Vector2`、`Vector3`、`Vector4` 与 `UIntVector2/3/4`，并根据退化输入语义选择 `try_normalize()`、`normalized_or_zero()` 或 `normalize_unchecked()`。`vec*`、`mat*`、`quat` 仍是受控迁移期的旧 GLM aliases，不得新增调用点。渲染坐标与矩阵约定以 `AGENTS.md` 和 `document/core-math-design.md` 为准，不在业务模块建立另一套类型别名。

矩阵使用 `Matrix3`、`Matrix4`，通过 `at(column, row)` 与 `data()` 访问 column-major 数据。可能奇异的求逆必须使用 `try_inverse()`；空间变换根据语义选择 `transform_position()`、`transform_vector()` 或 `try_transform_normal()`。

旋转使用 `Quaternion`，其连续存储顺序固定为 `x, y, z, w`。axis-angle、rotation matrix 和 direction-to-direction 构造使用对应 `try_make_quaternion_*` API；退化输入失败且不修改输出。比较旋转语义使用 `is_nearly_same_rotation()`，不能用分量相等代替，因为 `q` 与 `-q` 表达同一旋转。

TRS 使用 `Transform`，通过 `to_matrix()` 与 `try_decompose_transform()` 在值和矩阵之间转换；decomposition 只接受能以 positive-scale TRS 重建的 affine matrix，shear、mirror、zero scale 或非有限输入失败且不修改输出。position、vector 和 direction 必须分别使用对应具名函数，Camera/Light axes 使用 direction 语义避免 scale 泄漏。

forward/up rotation 使用 `quaternion.h` 中的 `try_make_rotation_from_forward_up()`；正交轴只是实现细节，不公开独立 basis 值。输出为 Matrix3/Matrix4、且不属于更具体值类型的纯语义构造统一放在 `matrix_construction.h`，当前包括 orientation-based view、target-based LookAt 和 reversed-Z perspective。Transform conversion、matrix algebra、Camera policy 与 backend correction 不得进入该模块。所有 checked construction 遇到非有限、退化或非法参数时不修改输出；业务调用方负责记录对象上下文与诊断。

共享平面和凸体分别使用 `math/geometry/plane.h` 与 `math/geometry/convex_volume.h`。`try_make_plane()` 生成正半空间为内部的归一化平面；`try_make_reversed_z_frustum()` 按引擎固定的 left-handed、0..1 reversed-Z contract 生成 finite 六面或 infinite-far 五面凸体。AABB 测试直接传入 minimum/maximum 值，接触平面视为相交；RenderScene 的 visibility policy 和空间索引不进入 `Toy3dMath`。

Windows 上 `Toy3dMath` 通过 PUBLIC compile definition 传播 `NOMINMAX`，避免 `windows.h` 的函数式宏破坏公共 `toy3d::min/max` contract；第一方目标应通过 CMake target 链接 `Toy3dMath`，不得在调用点重新引入竞争宏策略。

## PixelFormat

`Toy3dPixelFormat` 是 stateless bridge，供 runtime、editor、tools 与公共 RHI 共享规范化 GPU-ready `PixelFormat`。调用方链接该 target 并包含 `pixel_format/pixel_format.h`；不得让 Asset/Editor 为使用格式而依赖公共 RHI。

`pixel_format_block_width()`、`pixel_format_block_height()` 与 `pixel_format_bytes_per_block()` 返回存储 block geometry；Unknown/Max 返回 0。`pixel_format_calculate_minimum_row_pitch()` 与 `pixel_format_calculate_minimum_slice_pitch()` 对 block count 向上取整，并遵守 PVRTC 每维至少两个 blocks 的最小存储范围；非法 format、零 extent 或算术溢出返回 `false` 并把输出清零。业务层负责附加 asset/subresource 上下文形成诊断。

该模块不解析 PNG/JPEG/DDS，不保存可重新 Cook 的 source data，不执行色彩转换，也不查询目标 GPU capability。Editor preview、Cook output 和 runtime `TextureDesc` 只能在 import/cook 已经生成 GPU-ready payload 后使用它；完整 format usage/sample support 仍由 Cook profile 和 `RHIDevice::format_capabilities()` 验证。`VkFormat`/`DXGI_FORMAT` 映射只存在于各 backend。

## Threading

- 短生命周期函数线程使用 `Thread(ThreadManager&, name, function)`，销毁前显式 `join()`。
- 需要 `init/run/stop/exit` 生命周期时实现 `Runnable`，通过 `RunnableThread::create()` 创建，并检查 `RunnableThreadCreateResult`。
- `Event` 支持 `AutoReset` 和 `ManualReset`；跨线程停止通常由 owner 调用 `trigger()` 或 `RunnableThread::request_stop()`。
- `Queue<T, QueueMode::Spsc>` 和 `Queue<T, QueueMode::Mpsc>` 都严格只有一个 consumer；`enqueue()` 失败必须处理，`dequeue()` 返回 `false` 表示当前为空。

```cpp
toy3d::ThreadManager thread_manager;
toy3d::Event finished(toy3d::EventMode::ManualReset);
toy3d::Thread worker(thread_manager, "Prepare", [&finished]()
{
    // CPU work
    finished.trigger();
});
finished.wait();
worker.join();
```

## Task Graph

`ThreadManager` 和 `TaskGraphInterface` 由 composition root 持有。创建后，外部 owner thread 必须先 attach 自己的 Named Thread；多线程模式下 `RenderingThread` 由 Render Thread attach，单线程模式下逻辑渲染线程映射为 `GameThread`。

```cpp
toy3d::ThreadManager thread_manager;
toy3d::TaskGraphConfig config;
config.multithreaded = false; // 最小示例由 GameThread 顺序处理全部目标。
auto created = toy3d::create_task_graph(config, thread_manager);
if (!created.succeeded())
{
    return;
}

std::unique_ptr<toy3d::TaskGraphInterface> task_graph = created.take_task_graph();
if (!task_graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded())
{
    return;
}

toy3d::GraphEventRef prepared = toy3d::dispatch_graph_task(
    *task_graph,
    "PrepareData",
    [](toy3d::NamedThread, const toy3d::GraphEventRef&)
    {
        // 只处理线程安全、生命周期明确的 CPU 数据。
    });

toy3d::GraphEventArray prerequisites{prepared};
toy3d::GraphEventRef consumed = toy3d::dispatch_graph_task(
    *task_graph,
    "ConsumeData",
    [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
    task_graph->get_render_thread(),
    &prerequisites);

const toy3d::TaskWaitResult waited = task_graph->wait_until_task_completes(
    consumed, toy3d::NamedThread::GameThread);
if (!waited.succeeded())
{
    return;
}

const toy3d::TaskGraphShutdownResult stopped = task_graph->shutdown(
    toy3d::TaskGraphShutdownMode::Drain);
if (!stopped.succeeded())
{
    return;
}
```

关键约束：

- Task 输入优先是 immutable value 或生命周期覆盖完成事件的共享所有权；不要捕获即将离开作用域的引用。
- `NamedThread::AnyWorker` 用于普通 CPU 任务；必须在指定线程执行的任务才投递到 `GameThread` 或 `RenderingThread`。
- prerequisite 用 `GraphEventArray` 表达；需要延迟当前事件完成时才调用 `completion_event->dont_complete_until()`。
- Named Thread owner 等待时会 helping；禁止任务等待自身或形成依赖环。
- 正常退出使用 `Drain`，需要快速终止时使用 `CancelPending`，并检查 shutdown result。
- 更完整的线程模型、错误语义和迁移顺序见 `document/threading-task-graph-design.md`。

## 新增公共能力前

先搜索本页、`engine/core/`、相关测试和专项设计。若能力属于跨 runtime/editor/tools 的通用文件、日志、线程、任务、时间、缓存等系统，应扩展独立 Core contract，不得在业务模块复制一套实现。
