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
- schema migration 是显式整候选转换并验证，不能靠默认填充静默迁移单位/格式。
- PropertyPath 用稳定 field/index/element ID/variant 身份；可重排集合不能把下标当永久身份。EditSession owner-thread 验证整个 patch，preview 成功才 publish，撤销/重做保持一致；Save 冲突检查和成功后清 dirty，失败不丢草稿。

ReflectedValue 是已注册 struct 的类型/版本/owned binary frame；YAML 表达为 `{type, schema_version, value}`，读取校验冻结 schema、字段、深度、数量、字节和尾随数据。`reflected_value_references` 按反射 schema 递归枚举 AssetRef（含嵌套/数组/variant），加入 Scene 依赖与索引验证，不靠任意 YAML 字段猜引用。Scene 严格读取当前 schema 7，旧 schema 拒绝；原始 description_bytes 用于保存冲突检测。

修改反射字段要补 codegen 输入、值/整体验证、序列化 round-trip、未知字段处理；运行时组件还需 [GameScene](gamescene.md) 与 Editor 接入。

真实声明片段，摘自 core/asset/mesh/static_mesh_asset_data.h；省略同一 struct 的其它字段，不是新类型或独立可编译文件：

```cpp
TOY3D_REFLECT_TYPE("toy3d.StaticMeshAssetData", 3)
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
- 当前 Scene schema 为 7、SceneActor schema 为 6；StaticMesh schema/render_geometry 为 3，单位厘米。旧二进制生产读取明确拒绝，测试中隔离的旧格式 fixture 不是兼容入口。

源代码入口：asset/asset_identity.h、asset_pair.h、asset_yaml.h、asset_meta.h、asset_index.h，DTO 在各资源模块。格式/单位改变必须提升正确版本并明确迁移或拒绝，不能修改 parser 后继续声称旧字节等价。

## 发布、恢复与操作

AssetPairStore 是配对一致性入口；调用方不能自己分别写 YAML/meta。服务内串行化发布，worker 在保护下读取稳定配对快照；当前不提供跨进程同时可见的双文件原子替换，也不承诺断电 durable。

- 新建/保存以受控事务文件记录新旧摘要，meta 先发布、description 最后作为 commit 点；单文件 atomic write 不代替此协议。
- 删除先隐藏 description，移动使用 move journal；恢复只操作所属、格式/摘要都验证的 staging/journal，不扫描并随意删除未知临时文件。
- 保存的冲突基线是同一次已验证读取的原始 description bytes；不能重新读一次后把外部改动当成本次原始数据。
- Copy 新 ID，Move 保持 ID/扩展名；强依赖阻止不安全删除。成功磁盘提交后 Catalog/UI 刷新失败单独报错，不能反向宣称保存未发生。
- Editor 只写源码侧 project/asset，不写 bin；目录组织由使用者决定，不在 core 写死业务命名空间。

完整失败/恢复用例：tests/asset_file_tests.cpp；编辑入口 asset/edit_session.h。`scan_asset_catalog(..., include_scenes=false)` 为 Shader Cook 收集 .asset 身份/依赖；Scene 原生设置由注册该类型的宿主验证，不由 Shader 工具解析。默认仍包含 Scene 并完成原有检查。当前 Catalog 扫描可能验证完整 meta/payload，内容规模扩大后的按需读取优化需独立评估，不能在文档宣称已有 lazy metadata。

## StaticMesh 与 Texture 生产链

StaticMesh：FBX/OBJ/glTF/GLB → Assimp → MeshDescription → CPU MeshBuilder → YAML/meta → runtime StaticMesh。

- bake 源层级变换、保持 origin、合并实例；源单位显式转厘米，不能无条件乘 100。材质仅保留 slot 名，不自动导入/猜测纹理。
- 当前一个 LOD、UV0/color/Tangent0；构建使用 Tools 私有的固定 MikkTSpace revision `3e895b49d05ea07e4c2133156cfa94369e19e409`，CMake 核对两份 upstream 文件 SHA256，离线源通过 TOY3D_MIKKTSPACE_SOURCE_DIR 指定。Runtime 不依赖 MikkTSpace。corner 结果按原顶点与完整 tangent/sign 拆分，返回 source_vertices 供骨骼 builder 同步复制全部 influences；不平均跨接缝切线。缺有效 UV 的资产标记 valid_tangent_frame=false、保留初始化的安全切线，可用于 NormalMap=Off；旧 payload 2 拒绝，需要离线重建。skeletal payload 3/metadata 2 复用同一几何能力。不承诺 morph，animation/camera/light 可按已有诊断忽略。
- MeshDescription 是 CPU 建模数据，StaticMesh render_geometry 是可加载渲染数据，runtime 不依赖 Assimp 或重新建模。
- 重导入先完整候选验证再替换，bounds/index/数量/有限值/材质槽都检查，失败保留旧资产。

Texture schema 2 显式保存 usage 与 flip_green：PNG/JPEG 导入为 GPU-ready 完整 mip 链。Color 使用 RGBA8 sRGB，RGB 在线性空间滤波再编码；LinearData 使用 RGBA8 UNorm；Normal 使用 RGBA8 UNorm，解码 XYZ、可在首层翻转绿色通道、归一化，并以向量滤波/归一化各 mip，正 Y 为运行时约定。alpha 始终在线性空间滤波。旧 schema 1 拒绝读取，重新导入受影响资产；内置已知 Color 输入已改为新描述，payload 不变。

Editor 导入可选择用途；Project 纹理预览的 Reimport 重新选择源图并沿用用途/翻转设置，可改用途后重新生成。worker 只准备 CPU 候选，GT 核对同一读取的原始 description baseline 后用 AssetPairStore 替换，保持 ID；失败保留旧资产。普通预览不改变资产。

- 当前 source 上限 32 MiB、单边 4096、output 128 MiB、临时预算 256 MiB；一次一个作业，worker 准备，GT 检验结果身份后发布。
- CPU PixelFormat 决定块布局，pitch 用 ceil 和格式最小块数计算；runtime 不重新解码源图。
- 不支持的输入/格式明确失败，不能按副文件名猜或丢 alpha 后成功。

## 缩略图与验证

缩略图缓存位于 /Saved/AssetThumbnails，以 AssetId、内容摘要、generator version 为身份；StaticMesh 使用独立 preview World、渲染和异步读回生成 PNG。Material/MaterialInstance 使用 S_MaterialPreview 球体及其已保存材质配置，与 live preview 共用独立 studio World 串行队列；generator version 为 3，失败不覆盖已有图像。失败/缓存丢失不回滚已保存资产，结果还须核对请求代次和当前资源。

代表性测试：core/tests/reflection_tests.cpp、serialization_tests.cpp；tools/reflection_codegen/tests/codegen_tests.cpp、resource_kind_tests.cpp；tools/asset_pipeline/tests/static_mesh_import_tests.cpp、asset_pipeline/tests/texture_import_tests.cpp；core/tests/material_asset_tests.cpp、tests/asset_thumbnail_tests.cpp；engine/editor/tests/workspace_tests.cpp、thumbnail_integration_tests.cpp。先从 CMake 确认 target/条件，构建受影响链，再测 round-trip、损坏/上限、发布中断恢复、ID/引用、旧版本拒绝和候选失败保留旧结果。

## 环境资产

可选 `AssetRef` 字段的空值限定为零 Asset/Subresource ID、空 expected_type 和 Strong；binary codec 保留此值，YAML 写 `null`，反射依赖枚举跳过它。依赖索引与必需引用仍要求有效身份和类型；非规范空引用拒绝。Scene schema 7 未配置环境使用这个空值，不生成占位环境依赖。

`EnvironmentAssetData` schema 1 使用 asset/meta pair，必需 `environment_mips` kind 2，payload 1。Core 验证并拒绝未知算法/orientation、依赖、subresource、额外必需 segment、非有限/负 RGB 和非 1 alpha；仅接受 2..512 二次幂六面 RGBA16F 与完整 mip 链。面顺序和 panorama 方向见 Shader 主文档。

`Toy3dAssetPipeline::import_environment_hdr` 接受 Radiance 2:1 panorama：源 32 MiB、最长边 4096、解码 RGBA float 128 MiB，预过滤总工作上限 128M samples；每点 samples 16..1024、默认 128，默认 face 64。mip 0 保留原环境，其余按 mip/(mip_count−1) 的 GGX N=V、NoL 权重生成。无法表示为有限非负 FP16 时失败，不截断。生成数据与描述以现有 AssetPairStore 原子发布，不依赖 runtime/editor。验证入口 `Toy3dTools.EnvironmentImport`。

Editor 的 Import Environment/Content Browser/drop 接受 .hdr，模态选择 face size 与 samples；worker 只构建 owned CPU 候选，GT 核对请求与写入根后发布项目资产。无 EXR 或运行时动态预过滤入口。
