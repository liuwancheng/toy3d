# 编辑器资源接入方案（Draft）

## 1. 目标与当前落点

本方案是 `establish-editor-resource-foundation` 完成后的接入计划，供下一轮 OpenSpec 提案评审。它不改变[编辑器资源基础设计](editor-resource-foundation-design.md)的 Asset 文件格式、反射用途标记或现有 GameScene 生命周期。

目标是让一份位于创作源目录的 Toy3d Asset 从磁盘进入资源索引，经过类型化加载和领域验证，构造成运行时对象，并能由 Editor 选择、编辑、保存和重新打开。第一条贯通链路使用**静态模型 Asset**，因为它同时验证大数据段、子资源、依赖和渲染对象构造。使用一份真实 FBX 验证源文件导入和 `StaticMesh` 加载，同时保留受控的简单网格 Asset 作为编解码边界测试。材质参数的实时预览作为下一条链路接入，并继续以 Shader `Properties` 为权威。动画、碰撞和场景各自有后续领域适配；不能因第一条链路选择模型而收窄通用资源接口。

当前仓库已具备 `Toy3dReflection`、`Toy3dSerialization`、`Toy3dResource`、反射生成器和五类测试 DTO；这些 DTO 位于 `engine/tools/reflection_codegen/tests/`，尚不是生产资源类型。`Toy3dEditor` 已接入 `EditorApplication` 和显示预览立方体的 `Game Viewport`，但没有资产索引或资源 UI。`Engine::initialize_file_system()` 将部署目录 `bin/asset` 对应的 store 以只读方式同时挂为 `/Engine` 和 `/Project`；构建会重新复制 `engine/asset` 到 `bin/asset`。因此 `bin/asset` 只能作为部署副本，不能充当 Editor 保存位置。`AssetId` 当前有解析与校验，但没有生产创建入口。

### 1.1 Editor 先行工作包：视口呈现与 Actor Hit Proxy

完整 Editor 接入前，先独立完成两项可由受控测试驱动的渲染能力。它们不要求资源浏览器、FBX 生产导入器或场景保存已经落地；测试入口使用现有立方体或受控网格。后续 Editor 只提供视口尺寸、显示位置、点击请求和 Actor 身份，不直接持有 RHI 对象。

**工作包 E1：离屏场景输出与 ImGui 显示（基础版已实现）。** `SceneRenderTargets` 的场景颜色目标仍用于 Forward Pass；RenderScene/Renderer 增加供 Editor 面板采样的最终颜色目标，Tonemap 写入该目标，再由 ImGui 图片命令合成到主窗口。当前只有一个场景面板，Game Thread 传固定的不透明逻辑纹理 ID，`ImGuiSystem` 仅允许字体与该 ID，Render Thread 将该 ID 解析为本帧视口纹理。目标尺寸由面板内容区域决定，零尺寸跳过场景绘制；resize 后旧纹理由渲染提交与后端延迟销毁机制保留至 GPU 不再使用。普通运行时仍直接输出到 presentation target。已通过立方体可见的实际窗口截图、Debug 构建和相关测试验证；面板 resize、最小化/恢复、单/多 Rendering Thread 模式及多视口动态纹理注册仍需单独验收。

**工作包 E2：Actor 级 `HitProxyPass`。** 命名对应 UE 的 `EMeshPass::HitProxy`，不复制 UE 的类型前缀或 UObject 体系。Editor 侧为 Actor 分配稳定的会话身份；Game→Render 的快照传递只读数字 ID，不传 Actor 指针。Pass 复用可见 `MeshBatch` 的几何与对象变换，使用固定 Shader 和深度测试，写入 `R32UInt` 或经 format capability 验证的等价无损格式，0 表示空白，遵循 reversed-Z。一个 Actor 的多个 mesh batch 写同一 ID；Gizmo 不进入第一版 ID pass，由 ImGui/ImGuizmo 输入命中优先处理。点击请求记录视口内物理像素坐标、视口 generation 与场景 generation；读回后对照当帧 ID 映射，再由 Game Thread 校验 Actor 仍存在。第一版只响应编辑模式的单击，点击空白清除选择；组件、section、框选、半透明选择与连续悬停后置。

E2 的公共 RHI 读回基础现已按[RHI 设计](rhi-design.md)增加单像素 `R32UInt` `RHIReadback`、`readback_texture_pixel()` 和基于 queue completion value 的非阻塞 `read_uint32()`；Vulkan 使用私有的 readback buffer，D3D11/D3D12 尚返回 `Unsupported`。它只完成 GPU→CPU 数据通道；Hit Proxy Shader/Pass、Actor ID 快照、过期请求过滤与 Editor 选中态仍须接线。点击不得 `wait_idle` 或等待整个 graphics queue；允许下一帧得到结果，过期请求丢弃。

```cpp
// 拟新增的上层语义；具体名称和 Result 类型在 RHI/Editor 提案中固定。
// Game Thread 提交本帧目标尺寸，并只使用已发布的纹理身份；渲染在 Rendering Thread 完成。
editor_viewport.request_extent(requested_extent);
EditorTextureId published = editor_viewport.presented_texture_id();
if (published.valid())
    ImGui::Image(to_imgui_texture_id(published), panel_size);

HitProxyRequest request{viewport_id, pixel, viewport_generation, scene_generation};
renderer.request_hit_proxy(request);
// 后续帧：只返回 Actor 会话身份，不向 Editor 暴露 RHI texture 或 RenderProxy。
HitProxyResult hit = renderer.poll_hit_proxy(request.id);
editor_selection.apply_if_current(hit);
```

E1 基础版已完成，下一步是 E2：两者共享视口尺寸、资源生命周期和 frame submission 边界；E2 的 ID 目标与读回保持独立，不把选取数据塞进 ImGui 纹理身份。E2 验证通过后，再扩展已有 Editor 宿主的输入路由、资源浏览器、模型导入和属性编辑。当前 `Application::on_build_ui()` 不暴露 Renderer；E1 由 Engine composition root 传递帧值，后续接线继续保持这一边界。

### 1.2 编辑器专用属性的宏边界

后续提案采用 `WITH_EDITOR`（编辑器行为和 UI）与 `WITH_EDITORONLY_DATA`（导入、重导入、Cook 所需的创作源数据）两个编译开关，命名与 UE 保持一致。`TOY3D_PROPERTY(..., Edit)` 仍只表示检查器可修改，不表示字段仅供 Editor 使用。Actor Transform、模型几何引用、材质参数等运行时需要的字段即使可编辑，也必须保留在运行时 DTO 和序列化 schema 中。视口选中态、Gizmo 状态、面板布局等临时 UI 状态由 Editor 持有，不写进 Asset。

生产资源类型应将编辑器专用的源文件路径、导入选项等放入独立的创作数据类型或 `.asset` 可选数据段；运行时领域 DTO 及公共反射 schema 保持稳定。创作源 Asset 保留这些数据以支持重导入，未来 Cook 产物显式排除；在 Cook 尚未实现时，运行时读取创作源 Asset 须明确忽略该可选段，保存时则遵守现有“完整保留未知段，否则只读”的规则。当前五类测试 DTO 和既有文件格式不在此轮重写；生产模型 DTO 的具体拆分由后续 OpenSpec 提案固定。

```cpp
// 拟新增：仅在 Editor/离线导入目标中编译；不改变运行时共享 DTO 的布局。
#if WITH_EDITORONLY_DATA
TOY3D_REFLECT_TYPE("toy3d.ModelImportData", 1)
struct ModelImportData {
    TOY3D_PROPERTY("source_uri", Edit)
    std::string source_uri;
    TOY3D_PROPERTY("import_scale", Edit)
    float import_scale = 1.0f;
};
#endif

// 运行时需要的字段即使在检查器中可编辑，也不受编辑器宏保护。
TOY3D_REFLECT_TYPE("toy3d.ModelAssetData", 1)
struct ModelAssetData {
    TOY3D_PROPERTY("mesh_subresource", Edit)
    AssetRef mesh_subresource;
};
```

CMake 必须以目标级编译定义明确给出 0/1；`WITH_EDITORONLY_DATA` 只能用于 Editor/离线工具独有的类型或目标。多个目标共享的头文件不得因宏取值不同而改变同一 C++ 类型的布局或生成注册表，否则会产生 ABI/ODR 与 schema 不一致。当前生成器只解析显式清单中的有限 C++ 形态，尚不保证识别 `#if`；落地时要让生成清单与宏配置一致，或把专用类型移到独立头文件并分别生成，不能仅给字段加宏就假定反射输出会同步裁剪。Hit Proxy 的通用 RHI 读回接口仍属于公共 RHI 能力，不受编辑器宏保护。

## 2. 建议的目标结构

```text
engine/asset/**/*.asset                 创作源文件，子目录由使用者决定
       │
       ▼
Editor 专用 FileSystem 实例              /Engine → engine/asset，可写
       │                                与 Engine 当前只读部署实例使用同一 Toy3dFileSystem 实现
       ├── inspect_asset → AssetIndex    只看索引、ID、依赖和类型
       └── load_asset → ModelAssetData   类型化候选、schema 迁移、领域校验
                          │
                          ├── 领域 blob codec → StaticMeshDesc → StaticMeshRef → World/预览
                          └── EditSession<ModelAssetData> → 原子保存到创作源文件

runtime 部署：engine/asset → bin/asset → /Engine 只读；只消费已验证 Asset/产物。
```

Editor 可使用第二个 `FileSystem` 实例表达不同的 mount 策略，但不得另写文件系统实现。Editor executable 的 composition root 持有创作 `FileSystem`、冻结的 `TypeRegistry`、`AssetIndex`、schema migration registry 和活动编辑会话；`EditorApplication` 仅借用这些对象，必须在它们销毁前退出。`Engine` 继续持有自己的只读运行时文件系统和 World。类型化 DTO 与数据验证放在可被 Editor、runtime 和 tools 共同依赖的资源领域目标；模型到 `StaticMesh` 的适配器放在 runtime，不让 `Toy3dResource` 依赖 RenderCore。

当前只有单仓库的 `engine/asset` 创作根。Editor 的启动配置须明确源目录，可由命令行指定并由开发构建提供默认值；不得从当前工作目录猜测或把绝对开发机路径写入 Asset。第一阶段用 `/Engine` 映射这份源目录，不扫描当前 `/Project` 别名，避免同一 Asset 被登记两次。将来独立项目目录落地后，`/Project` 才映射独立的 `<project>/asset`；这个演进需要单独的项目宿主方案，不强制 `mesh/`、`animation/` 等子目录。

## 3. 接入步骤与接口草案

下列片段区分**已有 API**与**拟新增 API**；签名是下一轮提案的设计输入，不表示这些新增入口已经存在。

### A. 创作目录与索引

在 Editor 入口创建可写 `DirectoryFileStore`，用现有 `FileSystem::add_mount()` 和 `freeze()` 把源目录挂到 `/Engine`。继续保持 Engine 自身部署 mount 只读。拒绝源目录不存在、不可写、与部署目录相同或不能安全解析的启动配置；失败时输出日志并向 Editor 呈现错误，禁止静默降级到 `bin/asset`。

拟在 `engine/resource/` 增加扫描入口：递归使用 `FileSystem::enumerate()`，仅对 `.asset` 调用 `inspect_asset()`，构建临时 `AssetIndex`，全部成功且强依赖无环后一次交给 owner。非 Asset 文件如字体和图标跳过；具有 `.asset` 后缀但头损坏、重复 ID、重复路径、越界索引或未知必需段必须报告具体路径。可先采用手动重新扫描，不引入文件监视器或数据库。移动与删除初期由受控命令执行，先验证冲突与引用，再修改磁盘并刷新索引；不承诺跨文件事务。

扫描仅依赖 Asset 外层。遇尚未注册的 `root_type`，仍可展示文件的 ID、路径和类型，但禁止类型化编辑或构造运行时对象，并给出“缺少资源类型注册”的错误。扫描不能因用户自行建立子目录而改变资源身份；路径移动后索引必须更新或重扫，既有 `AssetRef` 仍按 ID 解析。启动扫描失败不发布半成品索引，运行中重扫失败保持旧索引并提示文件路径。

```cpp
// 拟新增：调用方拥有结果，失败不替换已发布索引。
AssetResult<AssetIndex> scan_assets(const FileSystem& files,
    const VirtualPath& root, AssetFileLimits limits);

// 注册/查询接口已有；生成函数及扫描入口属于拟新增的生产接线。
TypeRegistry types;
ReflectionStatus registered = register_generated_model_asset_types(types);
ReflectionStatus frozen = types.freeze();
AssetIndex index = scan_assets(editor_files, asset_root, limits).value();
const AssetLocation* location = index.find(asset_id);
```

生产创建流程还需补充非零 128 位 `AssetId`/`SubresourceId` 生成入口，并在当前索引中查重。ID 生成是资源身份职责，不能以文件名、数组下标或源节点顺序代替。首次创建使用已有 `encode_asset_file()` 与 `FileSystem::write_binary_atomic(..., CreateNew)`；已有 `save_asset()` 仅替换已发布文件，不用于创建。创建成功后再更新 owner 的索引，索引提交失败须清理新文件或报告需要重新扫描的明确状态。

### B. 生产模型 DTO 与构建生成

拟建立 `engine/resource/model/` 的共享目标 `Toy3dModelAsset`，包含稳定字段名的运行时 `ModelAssetData`、模型领域 validator 和生成的注册/编解码代码。运行时字段包含稳定 source key、mesh 子资源 ID、material/skeleton 引用和几何段名称；源 URI 与导入设置进入 Editor/离线工具独有的创作数据段，按 1.2 的宏边界处理。原始指针、`StaticMeshRef`、RHI 句柄均不持久化。CMake 以显式头文件清单调用 `Toy3dReflectionCodegen`，输出仅进入构建目录。当前测试 fixture 可作为行为样例，不应直接作为生产头文件链接。

第一轮提供一个离线小工具或受控测试入口，写入固定的三角形/立方体模型 Asset，以独立验证自定义 Asset 的编解码。同时增加 FBX 导入入口，把一份真实静态模型转换为相同的模型 DTO 与几何 blob。几何段使用模型领域自己的版本化、长度受限 codec；反射只编码模型元数据。几何 blob 的顶点、索引、section、bounds 和坐标约定需被领域验证，不能直接 dump `StaticMeshDesc` 内存。Asset 的依赖与子资源索引必须与 DTO 及 blob 一致。

#### FBX 导入库调研与首版边界

GitHub 上可用的候选有 [Assimp](https://github.com/assimp/assimp)、[ufbx](https://github.com/ufbx/ufbx) 和 [OpenFBX](https://github.com/nem0/openfbx)。三者都能读取 FBX。按当前项目的多格式资产导入方向和使用者偏好，**首版选择 Assimp**；它把 FBX 等格式导入统一的场景、节点、网格与材质表示，并提供三角化等后处理，采用 BSD 3-Clause 许可。其 [CMake 构建选项](https://github.com/assimp/assimp/blob/master/Build.md)允许关闭默认启用的其他 importer、exporter、工具、样例和第三方库测试，首版应只启用需要的 FBX importer，版本固定并记录许可证。这个选择不以 GitHub star 数单独证明 FBX 兼容性，仍须用实际 FBX 样例验收。

本机 `D:/GitProject/Piccolo` 当前不是 Assimp 案例：`engine/3rdparty/CMakeLists.txt` 接入 `tinyobjloader`，`engine/source/runtime/function/render/render_resource_base.cpp` 用 `tinyobj::ObjReader` 读取 `.obj`，并另有 JSON `MeshData` 路径；未查到 FBX importer。Piccolo 可参考模型资源与渲染数据分层，但其源文件解析路径不能直接作为 Toy3d 的 FBX 方案。

导入器放在 `engine/tools/`，只用于 Editor/离线导入，不让 runtime 或 `Toy3dResource` 链接 Assimp。源 FBX 经已有文件接口读取后交给 `Assimp::Importer::ReadFileFromMemory()`；外部引用文件由受控的导入 I/O 策略解析，不能让 FBX 中的路径绕开共享 FileSystem。遍历 `aiNode` 层级中的网格引用，读取 `aiMesh` 的 position、normal、uv0、可选 color 和 face，按 `mMaterialIndex` 生成 Toy3d section，并生成索引。一个 `aiMesh` 可被多个节点引用，必须明确实例变换和材质绑定策略，不能只遍历 `aiScene::mMeshes` 后丢掉节点语义。[Assimp 官方使用文档](https://github.com/assimp/assimp-docs/blob/master/source/usage/use_the_lib.rst)描述了场景、节点和网格的基本结构。

导入选项明确归一化为 Toy3d 的左手、Y 上、Z 前、米单位；三角化与坐标变换的 Assimp post-process flags 必须成组确定，并用实际样例检查绕序、法线、UV 和节点变换。单位换算也必须有明确策略，不能假定所有 FBX 使用同一单位。首个验收样例限制为**无骨骼的静态 FBX**；碰到 skin、blend shape 或不能正确表达的多实例/材质特性时，记录明确错误并拒绝该次导入，不得默默丢弃数据并宣称成功。

`StaticMeshDesc` 当前仅包含 position、normal、uv0、可选颜色、索引、section 和非空运行时 `MaterialInstanceRef`。因此首版 FBX 验证要选一份含两个材质分组、UV 接缝及非单位缩放的模型；尚未实现材质 Asset loader 时，在测试/预览边界显式提供占位 `MaterialInstanceRef`，同时保存源材质槽信息以供后续建立引用。接受标准是 FBX → 自定义 `.asset` → 重新加载 → `StaticMesh::create()` → 可见渲染，且重启后结果一致；直接从 FBX 创建 `StaticMesh` 只能算导入器局部测试，不能代替 Asset 链路验证。

```cpp
// 拟新增：导入器只返回候选领域数据，发布仍由 Asset owner 负责。
ImportResult<ModelAssetCandidate> import_fbx_static_mesh(
    const FileSystem& source_files, const VirtualPath& source_path,
    const FbxImportOptions& options);

auto candidate = import_fbx_static_mesh(source_files, fbx_path, options);
auto encoded = encode_model_asset(candidate.value());
auto published = publish_asset(editor_files, target_asset_path, encoded.value());
auto mesh = load_static_mesh_asset(runtime_files, target_asset_path, preview_materials);
```

领域层须从候选 DTO 重新推导 `AssetFileIndex` 中的依赖和子资源，并与准备保存的 blob 段目录一起验证。编辑 `AssetRef` 或子资源后不能沿用旧索引直接保存，否则文件索引与类型数据会分叉。当前 `AssetIndex` 只有 `add()`/`move()`，尚无替换已登记文件元数据的发布入口；下一轮需增加受 owner 串行控制的候选替换，先预检冲突和强依赖环，文件保存成功后更新内存索引。若内存更新出现意外失败，应标记索引需重扫并阻止继续编辑该文件。

### C. 运行时模型适配

在 runtime 的模型/几何边界增加适配器：通过已有 `load_asset()` 得到完整 DTO，再按索引读取几何段，解析为候选 `StaticMeshDesc`；解析、引用和领域验证成功后调用 `StaticMesh::create()`。材质依赖初期可由现有项目案例提供明确的 `MaterialInstanceRef`，同时验证 AssetRef 与 `AssetIndex` 的身份；后续由材质 Asset loader 解析，不能把运行时 shared pointer 写回 Asset。候选构造失败保持旧 `StaticMeshRef` 和旧预览。

```cpp
// 已有 API：先检查每个状态，再使用 value()。
AssetResult<AssetFileIndex> summary = inspect_asset(files, path);
ModelAssetData model{};
AssetStatus loaded = load_asset(types, schemas, files, path,
    "toy3d.ModelAssetData", model, validate_model);
AssetResult<std::vector<std::uint8_t>> geometry =
    read_asset_segment(files, path, summary.value().asset_id, geometry_desc, max_geometry_bytes);

// 拟新增：领域适配器负责 blob codec 与运行时对象候选。
ModelRuntimeResult candidate = build_static_mesh(model, geometry.value(), resolved_materials);
```

首先用 `Toy3dCubeTest` 或等价验证程序，把当前硬编码网格改为从一份由真实 FBX 导入的 Asset 加载的候选，并保持旧启动路径作为迁移期间的回退测试。达到同等渲染和释放行为后，再删除重复创建路径；不要把文件 I/O 放到 Rendering Thread。

### D. 最小 Editor 工作流

Editor 新增 `EditorApplication` 并通过已有 `Engine::set_application()` 注入；它借用 Editor composition root 中的资产工作上下文，在 Game Thread 使用 `Application::on_build_ui()` 绘制初版 UI。先做资产列表、按索引显示类型/ID/依赖、选中模型的只读详情，再增加由 `TypeRegistry` 与 `PropertyPath` 驱动的可编辑字段。每个编辑调用 `EditSession<ModelAssetData>::apply_edit()`；撤销、重做和保存分别走会话已有入口。编辑 import settings 应显示“需要重新导入/构建产物”的状态，不把仅修改元数据误报成已经更新预览。

保存时提供旧文件所有非 `type_data` 段的字节，缺失时保持只读并显示 `AssetStatus`。关闭或切换 Asset 时处理脏会话。Editor 调用方负责把状态映射到现有 Logger 与需要用户操作的 Dialog，不建立第二套诊断系统。第一阶段不实现通用方法调用、运行时 Actor 任意字段改写、资源热重载或多文档并发编辑。

`EditSession::save()` 目前只比较已发布的 `type_data` 与会话基线。模型导入器若在会话打开后改写几何 blob，单靠此比较不足以识别冲突；第一版在同一 owner 下串行化导入与编辑保存，并在保存前比对目标文件的版本或完整内容摘要。没有这项保护时，模型 Asset 只允许只读查看或拒绝保存含 blob 的会话，不能把旧 blob 重新发布覆盖导入结果。未知可选字段或段无法无损保留时同样保持只读。

材质作为第二个编辑链路：读取 Shader 已验证的 `Properties`/`ShaderParameterId`，校验 material override 身份、类型、范围与 orphan；值提交后通过 `MaterialInstance` setter 或 material replacement 入口预览。材质参数 schema 不能由通用反射重复登记，RenderProxy 不能由检查器直接写。

## 4. 后续资源类别的接入边界

| 资源 | 创作数据与产物 | 领域适配前置条件 | Editor 第一版行为 |
| --- | --- | --- | --- |
| 模型 | source key、子资源、引用、独立几何段 | 模型 blob codec、FBX 静态网格导入、`StaticMeshDesc` 构造；后续扩展其他源格式 | 浏览、元数据编辑、保存、预览已构建几何 |
| 材质 | Shader 引用、稳定参数覆盖 | Shader `Properties` 只读 schema、材质运行时适配 | 参数编辑、orphan 提示、setter 预览 |
| 动画 | 骨架引用、轨道、key、事件；后续压缩段 | 轨道目标与骨架解析、时间校验、动画运行时对象 | 列表与关键帧检查，后续时间线 UI |
| 碰撞 | shape tagged variant、mesh 引用；后续加速数据 | 物理后端形状构造与重建边界 | 形状列表和参数验证，后续空间预览 |
| 场景 | Actor/Component ID、层级、附件、资源引用 | GameScene 未发布候选与受控生命周期创建入口 | 可先查看 DTO，暂不直接装配 World |

FBX 静态网格导入纳入首条模型验证链路；其他源格式、FBX 动画与蒙皮、动画压缩、物理 Cook、场景装配和独立项目目录应分别立 OpenSpec change。它们共享 Asset 外层与稳定身份，但各自定义领域 blob、验证、预览和发布策略。

## 5. 分批实施与验收

1. **Asset workspace**：源目录启动配置、可写 Editor mount、递归扫描、临时索引发布、ID 生成、创建/重复/损坏/只读测试。验收：重启后仍以同一 Asset ID 找到文件；Editor 保存不触碰 `bin/asset`。
2. **模型生产链**：正式 DTO、构建生成、简单网格 Asset 写入器、模型 blob codec、Assimp 静态 FBX 导入、runtime `StaticMesh` 适配。验收：真实 FBX 导入为 `.asset` 后，测试或 Cube 案例从磁盘重新加载网格并渲染；UV 接缝、材质 section、坐标/单位转换正确；坏 blob、丢失依赖、子资源重排不污染旧对象。
3. **最小 Editor**：`EditorApplication`、资产列表、模型详情、字段编辑、undo/redo、保存、错误提示。验收：编辑后重启可读回；保存失败保留脏状态与旧文件；改 import settings 明确显示待重建。
4. **材质编辑预览**：生产材质 DTO、Shader schema 适配、Asset 保存、setter/候选预览。验收：参数类型、范围和 orphan 由 Shader schema 决定；失败保留旧材质预览。
5. **后续领域**：其他模型源格式、FBX 动画与蒙皮、碰撞、场景分别推进，不合并成一个大 Editor 任务。

每批都要求 Windows 配置、受影响目标构建和相关测试；共享目标与生成器还需 macOS/Clang 验证（可用环境下）。文件格式与 schema 迁移、非法字段、原子写失败、索引冲突要保留自动化验证。跨平台验证缺席时如实记录，不能据 Windows 构建宣称其他平台已支持。

## 6. 提案前需要固定的决策

- **创作根**：建议当前单仓库使用 `engine/asset`，Editor 显式绑定源目录；`bin/asset` 永远是部署副本。未来独立项目目录另案引入。
- **首个资源**：建议先用受控简单网格 Asset 验证 codec，再用 Assimp 将真实静态 FBX 导入同一 Asset 格式，完成运行时 `StaticMesh` 构造与渲染，然后做材质参数编辑预览。FBX 解析只存在于 Editor/离线导入工具。
- **Editor 所有权**：建议 Editor executable 拥有独立的 Toy3dFileSystem 实例及资源工作上下文，并注入 `EditorApplication`；不把可写资源服务塞进通用 `Engine` 或 `Application` 基类。
- **发布范围**：本轮仅单文件 Asset 原子保存；多文件导入/Cook 的 publication 和资源缓存失效单独设计。

这些决策确认后，再以独立 OpenSpec change 固定第一批 workspace 与模型生产链的 proposal、spec、design 和 tasks；Editor UI 与其余资源类别按可独立验收的批次推进。
