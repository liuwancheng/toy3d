# Assets：反射、持久化与生产链

## 定位与边界

CPU DTO/验证/格式在 engine/core，导入与构建在 engine/tools，运行时适配在 runtime，交互/保存策略在 Editor；依赖不能反转。反射不是运行时对象系统，序列化不识别 Actor/Proxy/RHI 原生对象。

| 能力 | target / 主要代码 |
| --- | --- |
| 反射元数据 | Toy3dCore：reflection/；Toy3dReflectionCodegen：tools/reflection_codegen/ |
| 编解码/迁移 | Toy3dCore：serialization/ |
| 身份、描述、meta、事务、索引、编辑 | Toy3dAssets：core/asset/ |
| MeshDescription/StaticMesh | Toy3dAssets：core/asset/mesh/ |
| Material/Scene/Texture DTO | Toy3dAssets：core/asset/{material,scene,texture}/ |
| 导入/构建 | Toy3dAssetPipeline：tools/asset_pipeline/，Assimp 私有且可关闭；CLI 为 Toy3dModelImport / Toy3dDefaultAssets |
| 外部缩略图缓存 | Toy3dAssets：core/asset/thumbnail/ |

下文 core 路径相对 engine/core，工具相对 engine/tools；runtime/Editor 行为分别见 [Material](material.md)、[Editor](editor.md)、[Renderer](renderer.md)。

## 反射与序列化

- reflection_macros.h 的标记不改变 C++ ABI；仅显式标记的 public struct/field/enum 进入 codegen。类型/字段身份稳定 ASCII，标记与声明分行，不把未标记成员自动扫描成 contract。
- Edit 表示可编辑，Visible 表示只读显示，Transient 不保存；未标记排除，已标记且非 Transient 才持久化。UI hints 不代替值/整体对象验证。
- CMake 显式登记输入，codegen 不做完整预处理/include 展开；标记后的字段声明可跨行，到分号结束，换行不改变字段语义；支持受控数值/string/math/嵌套/vector/有限 variant/AssetRef，拒绝指针、private、未知类型及不完整声明。生成文件进入 build，不手改产物。
- TypeRegistry 显式注册、冻结，各可执行文件只注册需要的 schema。DTO 不包含 runtime/editor/native handles。
- binary 编码固定宽度 little-endian；string 是有界 UTF-8，数组/嵌套深度/总字节均有界；错误含 offset/path。未知 required 拒绝；UnknownOptionalField 必须保持只读，不能未知数据丢失后保存成功。
- `ValueReader` 的 uint8/uint16/uint32/float32 批量读取不附加长度前缀，由调用方先读取数量；检查总字节、元素上限、目标指针和剩余长度，float32 逐值拒绝非有限数。失败保持目标数组及 reader 位置不变，非有限数错误 offset 指向该值；空数组允许空指针，输出存储不得与输入字节重叠。密集数值载荷（如 Environment mip）按整块读取，不逐元素走标量 codec。不依赖 struct padding、主机字节序或原生对象布局。Reader 及输出仍由调用线程独占，输入保持原有非 owning 生命周期。
- `AssetResult::value()` 的右值重载用于把已检查成功的 owned 候选移动给下一层；左值仍只读。消费后不得再次把该结果当成完整资产使用；错误与持久化格式不变。实现归 Toy3dCore/Toy3dAssets，边界验证入口为 `Toy3dCore.Serialization` 和资产几何/动画测试。
- schema migration 是显式整候选转换并验证，不能靠默认填充静默迁移单位/格式。
- PropertyPath 用稳定 field/index/element ID/variant 身份；可重排集合不能把下标当永久身份。EditSession owner-thread 验证整个 patch，preview 成功才 publish，撤销/重做保持一致；Save 冲突检查和成功后清 dirty，失败不丢草稿。

ReflectedValue 是已注册 struct 的类型/版本/owned binary frame；YAML 表达为 `{type, schema_version, value}`，读取校验冻结 schema、字段、深度、数量、字节和尾随数据。`reflected_value_references` 按反射 schema 递归枚举 AssetRef（含嵌套/数组/variant），加入 Scene 依赖与索引验证，不靠任意 YAML 字段猜引用。Scene 严格读取当前 schema 7，旧 schema 拒绝；原始 description_bytes 用于保存冲突检测。

修改反射字段要补 codegen 输入、值/整体验证、序列化 round-trip、未知字段处理；运行时组件还需 [GameScene](gamescene.md) 与 Editor 接入。

真实声明片段，摘自 core/asset/mesh/static_mesh_asset_data.h；省略同一 struct 的其它字段，不是新类型或独立可编译文件：

```cpp
TOY3D_REFLECT_TYPE("toy3d.StaticMeshAssetData", 4)
struct StaticMeshAssetData
{
    TOY3D_PROPERTY("material_slots", Visible)
    std::vector<std::string> material_slots;
    // 其余字段见原头文件；Visible 只决定编辑呈现，仍需格式/值验证。
};
```

type string/schema version 是持久化身份，Visible 字段仍序列化，只有 Transient 排除；不能从 UI 是否可编辑推导是否保存。

## Asset 身份与配对格式

- AssetId 为非零 128-bit 身份，小写 32 位 hex；随机生成，不能来自路径/hash。AssetRef 包含 ID、subresource、类型与引用强度。
- 强依赖、重复 ID、类型/subresource 不匹配、强循环均在 Catalog 候选完整验证；失败保留旧索引。稳定 subresource key 支持重导入，失去的旧 key 明确 orphan，不能悄悄指向另一个对象。
- 描述文件为 YAML：通常 .asset，.scene 仅 SceneAssetData。format_version=2，核心字段为 asset_id/root_type/schema_version/dependencies/subresources/data，处理数据通过可选 meta 描述（format_version/size/sha256/required_segments）。
- parser 有输入/深度/数量边界，拒绝重复键、aliases/tags、非法 UTF-8、非有限数、未知结构；root_type 决定类型，不能只看扩展名。
- 配对文件是同目录同 stem 的 .meta。TOY3DMTA1 包含 AssetId、段目录、长度与 SHA-256；必需段缺失或损坏不能加载正常资产。StaticMesh 使用 render_geometry，Texture 使用 texture_mips，Material 是纯 YAML；Scene 当前拒绝 .scene.meta。
- meta 不收原始 source_mesh/import_data/thumbnail。缩略图是可重建外部缓存，资产描述不能依赖它。
- 当前 Scene schema 为 7、SceneActor schema 为 6；StaticMesh 描述 schema 为 4，render_geometry 为 3，单位厘米。SkeletalMesh 描述 schema 为 3，skeletal_geometry 为 3。仅上一版网格 YAML 描述的显式材质字段迁移可读，见下节；旧几何及旧二进制生产读取仍拒绝，测试中隔离的旧格式 fixture 不是兼容入口。

源代码入口：asset/asset_identity.h、asset_pair.h、asset_yaml.h、asset_meta.h、asset_index.h，DTO 在各资源模块。格式/单位改变必须提升正确版本并明确迁移或拒绝，不能修改 parser 后继续声称旧字节等价。

## 资产加载门面

加载缓存持有 CPU 资产身份，不决定共享设备资源的释放。作者场景/PIE 可共享同一运行网格；proxy 的渲染使用引用及可回收驻留见 [Render Framework](render-framework.md#共享资源生命周期)。

`engine/runtime/asset_loader` 的 AssetLoader 是资产对（asset pair）解码的统一入口，每个进程只有一个实例：Engine 创建并持有它，在 `on_initialize` 之前通过 `Application::set_asset_loader` 注入给应用（Editor 与 Game host 用同一份），取代各调用点自行决定线程/缓存/策略的做法。门面本身不认识任何载荷类型：扩展一种资源 = 在对应 decoder 旁新增一个 `AssetLoadJob` 子类（`decode` 在加载线程产出 owned CPU 数据、`adopt` 在 GT 创建运行时对象、`bytes` 供缓存计量）+ 一个类型化入口（现有：`request_texture`/`load_assembly_texture` 与 `request_static_mesh`/`load_assembly_static_mesh`），门面的队列/优先级/single-flight/缓存/失效/有界等待逻辑完全复用。请求由 `AssetHandle<T>` 返回（`pending/ready/failed/invalidated`，`get()` 是请求时绑定的类型化取值器，调用点不做转换）。缓存、失败记忆与在途登记都按 identity 索引，而句柄的取值器会把 Job 向下转型，因此同一 identity 若以另一种 `expected_type` 再次请求，会得到显式诊断（"already known as X but requested as Y"）而不是拿到错误类型的 Job；失败记忆同样带类型，避免把一种类型的失败报告给另一种请求。`MaterialLibrary` 这类需要"立即拿到 TextureRef"的组件不再自己解码，而是由 composition root 注入 resolver：构建 runtime 材质需要贴图就位，因此 resolver 走 `load_assembly_texture`（Critical + 有界等待，解码仍在加载线程）。档位现状：Critical 由装配路径显式传入；Environment 类型默认为 High（预览窗口的环境请求即走该默认）；其余请求默认 Normal；**Low 目前没有调用点**，保留给后续迁移的预取/缩略图路径，档位顺序由 `priority_bucket` 显式映射而非枚举序号。

加载线程只解码出 owned CPU payload（当前为 `TextureDesc` 与 `StaticMeshAssetGeometry`），GT 在 `tick()` 的 adopt 阶段创建运行时对象（Texture / StaticMesh）并交付等待者与缓存，运行时对象所有权不离开 GT。句柄状态为 `pending/ready/failed/invalidated`：`cancel()` 终结本次等待（无论解码是否已经开始，该句柄都不会再收到结果），共享任务只有在**全部**等待者都已取消时才在开始前被丢弃，**丢弃句柄不等于取消**——同一 identity 的解码仍会完成并进入共享缓存，因此逐帧轮询的调用方既不中断加载也不重复解码；解码失败按 identity 记忆并在 `invalidate()` 前返回同一诊断，避免逐帧重启同一个失败解码；`invalidated` 表示该 identity 在解码在途时被失效，等待者应重新请求当前内容，而不是继续等一个不会到达的结果。被失效（stale）的解码结果整体丢弃；被取消的等待者只是不再收到投递，其结果仍会进入共享缓存。

缓存按 identity 存已 adopt 的运行时对象（以 Job 形态持有，`bytes` 由 Job 提供），超出字节预算时按最近使用淘汰（预算只统计 CPU payload，是软上限且不代表进程内存上限：GPU 资源与仍被句柄/场景引用的对象不会因淘汰而释放）；`invalidate(id)`（重导入/删除/来源失效）与 `invalidate_all()`（Catalog 重扫、工程切换）丢弃缓存、清除失败记忆、解除受影响 identity 的在途登记，并把仍在等待的句柄标记为 `invalidated`；因此失效后立刻到来的请求一定由新的解码满足，而不会挂在一个永远不会交付的候选上，旧等待者也不会被永久搁置。缓存只能由 adopt 写入，没有旁路入库接口。摘要全量校验只留在发布/导入边界，不做按内容摘要的惰性失效。不声明优先级时按 `default_asset_load_priority` 的类型档位请求，避免同类资产在不同窗口落到不同档；Critical 由无法在缺资产时继续的装配路径使用（Scene 装配的环境与 StaticMesh、PIE、World Settings、Game 启动 Scene 与材质贴图解析、材质窗口的 Texture2D 参数槽，以及两个预览窗 `initialize` 的首帧 cube），它们用 `wait()` 做有界等待；窗口与缩略图的 Environment cube 逐帧轮询。StaticMesh 装配（Scene/PIE/Game 启动）已接入门面，Scene graph 解析与骨骼/动画解码仍走各自既有路径，迁移按调用点分批进行；非资产对解码（如缩略图源图 PNG）不在门面范围内。rendercore 的 `build_environment_texture_desc`/`build_texture2d_desc`（`texture_asset_decode.{h,cpp}`）是门面与 `TextureLoadJob` 共用的纯 CPU 解码缝：**资产解码路径里**运行时对象只在 Job 的 `adopt` 创建（`TextureLoadJob::adopt`），内置默认贴图（`material_asset_builder.cpp`）与引擎基础几何（`scene_geometry.cpp`）不走门面，仍各自调用 `Texture::create`。

加载位置的线程契约见 [Threading](threading.md#资源加载线程)。

## 发布、恢复与操作

AssetPairStore 是配对一致性入口；调用方不能自己分别写 YAML/meta。服务内串行化发布，worker 在保护下读取稳定配对快照；当前不提供跨进程同时可见的双文件原子替换，也不承诺断电 durable。

- 新建/保存以受控事务文件记录新旧摘要，meta 先发布、description 最后作为 commit 点；单文件 atomic write 不代替此协议。
- 删除先隐藏 description，移动使用 move journal；恢复只操作所属、格式/摘要都验证的 staging/journal，不扫描并随意删除未知临时文件。
- 保存的冲突基线是同一次已验证读取的原始 description bytes；不能重新读一次后把外部改动当成本次原始数据。
- Copy 新 ID，Move 保持 ID/扩展名；强依赖阻止不安全删除。成功磁盘提交后 Catalog/UI 刷新失败单独报错，不能反向宣称保存未发生。
- Editor 只写源码侧 project/asset，不写 bin；目录组织由使用者决定，不在 core 写死业务命名空间。

完整失败/恢复用例：tests/asset_file_tests.cpp；编辑入口 asset/edit_session.h。`scan_asset_catalog(..., include_scenes=false)` 为 Shader Cook 收集 .asset 身份/依赖；Scene 原生设置由注册该类型的宿主验证，不由 Shader 工具解析。默认仍包含 Scene 并完成原有检查。当前 Catalog 扫描可能验证完整 meta/payload，内容规模扩大后的按需读取优化需独立评估，不能在文档宣称已有 lazy metadata。

## 网格默认材质

StaticMeshAssetData / SkeletalMeshAssetData 保存 `default_materials: vector<AssetRef>`，按已有 material_slots 顺序对应。空数组表示所有槽使用调用方引擎默认材质；非空必须与槽数一致，单个规范空引用表示该槽未赋值。非空引用仅接受 Strong、无 subresource 的 Material/MaterialInstance，引用必须出现在去重后的 dependencies；SkeletalMesh 还保留 Skeleton 强依赖。core/asset/mesh/mesh_materials 提供格式验证、依赖收集和按唯一槽名的重导入映射，不持有运行材质或 UI。

StaticMeshAssetGeometry 携带从描述读出的默认引用，作为加载适配器的 owned 输入；其 render_geometry 编码不存引用，嵌套的骨骼几何也不改变 payload。修改默认材质只改变 YAML 描述和依赖；Editor 保存完整保留已验证 meta，包括未知可选段，不重新构建顶点、权重或 bind 数据。

注册网格 schema 时显式登记 StaticMesh 3→4、SkeletalMesh 2→3 的历史根描述和 SchemaMigrationRegistry step，添加空默认材质数组。TypeRegistry::add_previous_schema 在 freeze 前登记历史描述和迁移；find(name) 仍只返回当前 schema，find(name, version) 仅供读取登记过的版本。YAML reader 先按源描述严格解码，再迁移并验证当前完整候选，返回当前 schema/type_data，原始 description_bytes 保持不变。打开和扫描不改磁盘；用户保存才写当前 schema。未登记旧版、未来版或含未知 typed 数据均不能静默保存成功，不自动迁移旧几何或其他资产类型。

现有骨骼重导入按唯一槽名保留默认材质，已有赋值失去或出现歧义时拒绝该候选并诊断；不按新槽下标猜映射。StaticMesh 导入仍只创建新资产。运行加载、场景覆盖和草稿边界见 [Editor](editor.md#网格材质槽编辑)，MaterialLibrary ownership 见 [Material](material.md)。

## StaticMesh 与 Texture 生产链

StaticMesh：FBX/OBJ/glTF/GLB → Assimp → MeshDescription → CPU MeshBuilder → YAML/meta → runtime StaticMesh。

- bake 源层级变换、保持 origin、合并实例；源单位显式转厘米，不能无条件乘 100。材质仅保留 slot 名，不自动导入/猜测纹理。
- 静态导入在 bake 后按现有法线归一化容差跳过有限的退化三角形，并按源网格报告跳过数量；所有网格均无有效三角形时明确失败。非法索引、非有限值、溢出和不可逆变换仍拒绝。MeshDescription/产物验证保持严格，不把退化面写入资产。
- 当前一个 LOD、UV0/color/Tangent0；构建使用 Tools 私有的固定 MikkTSpace revision `3e895b49d05ea07e4c2133156cfa94369e19e409`，两份 upstream 原文件保存在 `engine/tools/asset_pipeline/thirdparty/mikktspace/`，保留版权声明与原始字节，CMake 核对 SHA256，直接使用本地源码，不联网下载。Runtime 不依赖 MikkTSpace。corner 结果按原顶点与完整 tangent/sign 拆分，返回 source_vertices 供骨骼 builder 同步复制全部 influences；不平均跨接缝切线。缺有效 UV 的资产标记 valid_tangent_frame=false、保留初始化的安全切线，可用于 NormalMap=Off；旧 payload 2 拒绝，需要离线重建。skeletal payload 3/metadata 2 复用同一几何能力。不承诺 morph，animation/camera/light 可按已有诊断忽略。
- MeshDescription 是 CPU 建模数据，StaticMesh render_geometry 是可加载渲染数据，runtime 不依赖 Assimp 或重新建模。`decode_static_mesh_asset_pair` 解码已由 AssetPairStore/read_asset_pair 验证的完整快照；`read_static_mesh_asset` 复用该解码入口，后台预览不另读一遍几何或分开读取 YAML/meta。
- 重导入先完整候选验证再替换，bounds/index/数量/有限值/材质槽都检查，失败保留旧资产。

Texture schema 2 显式保存 usage 与 flip_green：PNG/JPEG 导入为 GPU-ready 完整 mip 链。Color 使用 RGBA8 sRGB，RGB 在线性空间滤波再编码；LinearData 使用 RGBA8 UNorm；Normal 使用 RGBA8 UNorm，解码 XYZ、可在首层翻转绿色通道、归一化，并以向量滤波/归一化各 mip，正 Y 为运行时约定。alpha 始终在线性空间滤波。旧 schema 1 拒绝读取，重新导入受影响资产；内置已知 Color 输入已改为新描述，payload 不变。

Editor 导入可选择用途；Project 纹理预览的 Reimport 重新选择源图并沿用用途/翻转设置，可改用途后重新生成。worker 只准备 CPU 候选，GT 核对同一读取的原始 description baseline 后用 AssetPairStore 替换，保持 ID；失败保留旧资产。普通预览不改变资产。

- 当前 source 上限 32 MiB、单边 4096、output 128 MiB、临时预算 256 MiB；一次一个作业，worker 准备，GT 检验结果身份后发布。
- CPU PixelFormat 决定块布局，pitch 用 ceil 和格式最小块数计算；runtime 不重新解码源图。
- 不支持的输入/格式明确失败，不能按副文件名猜或丢 alpha 后成功。

## 缩略图与验证

缩略图是以 AssetId、内容摘要和 thumbnail_generator_version 为身份的可重建外部缓存，写 /Saved/AssetThumbnails，不修改资产描述/meta；失败或丢失不回滚已保存资产。各类型生成、线程、图片接管与缓存策略统一见 [Editor](editor.md#资产缩略图)。

代表性测试：core/tests/reflection_tests.cpp、serialization_tests.cpp；tools/reflection_codegen/tests/codegen_tests.cpp、resource_kind_tests.cpp；tools/asset_pipeline/tests/static_mesh_import_tests.cpp、asset_pipeline/tests/texture_import_tests.cpp；core/tests/material_asset_tests.cpp、tests/asset_thumbnail_tests.cpp；engine/editor/tests/workspace_tests.cpp、thumbnail_integration_tests.cpp。先从 CMake 确认 target/条件，构建受影响链，再测 round-trip、损坏/上限、发布中断恢复、ID/引用、旧版本拒绝和候选失败保留旧结果。

## 环境资产

可选 `AssetRef` 字段的空值限定为零 Asset/Subresource ID、空 expected_type 和 Strong；binary codec 保留此值，YAML 写 `null`，反射依赖枚举跳过它。依赖索引与必需引用仍要求有效身份和类型；非规范空引用拒绝。Scene schema 7 未配置环境使用这个空值，不生成占位环境依赖。

`EnvironmentAssetData` schema 1 使用 asset/meta pair，必需 `environment_mips` kind 2，payload 1。Core 验证并拒绝未知算法/orientation、依赖、subresource、额外必需 segment、非有限/负 RGB 和非 1 alpha；仅接受 2..512 二次幂六面 RGBA16F 与完整 mip 链。面顺序和 panorama 方向见 Shader 主文档。

`Toy3dAssetPipeline::import_environment_hdr` 接受 Radiance 2:1 panorama：源 32 MiB、最长边 4096、解码 RGBA float 128 MiB，预过滤总工作上限 128M samples；每点 samples 16..1024、默认 128，默认 face 64。mip 0 保留原环境，其余按 mip/(mip_count−1) 的 GGX N=V、NoL 权重生成。无法表示为有限非负 FP16 时失败，不截断。生成数据与描述以现有 AssetPairStore 原子发布，不依赖 runtime/editor。验证入口 `Toy3dTools.EnvironmentImport`。

Editor 的 Import Environment/Content Browser/drop 接受 .hdr，模态选择 face size 与 samples；worker 只构建 owned CPU 候选，GT 核对请求与写入根后发布项目资产。无 EXR 或运行时动态预过滤入口。
