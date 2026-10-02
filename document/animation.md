# Animation：Skeleton、SkeletalMesh 与 GPU Skin

> 本页是已确认设计及现有 contract 的唯一入口。资产格式、导入构建、CPU 播放、公共网格输入和 GPU Skin 资源基础已实现；World 组件、材质的 Local/GPUSkin permutation、三类 mesh pass 的 SkeletalMesh 接入、Editor 交互预览和 Scene 持久化仍按下文设计接入。基础资源可用不表示编辑器已能打开骨骼资产预览。

## 用例与范围

目标是导入带骨骼的模型和动画，在 Editor 中预览骨架、蒙皮网格及单个动画，在 Game/PIE 中复用同一播放与渲染路径。CPU 解码已构建动画、采样 local pose、计算骨骼层级和蒙皮矩阵；GPU vertex shader 执行 linear blend skinning（LBS）。runtime 不读取 FBX/glTF，也不依赖 Assimp。

首版范围：一个 LOD、多材质 section、每顶点最多 8 个 influence、4/8 两档紧凑存储与共用 GPUSkin shader、单 AnimationSequence 播放/暂停/循环/定位/倍速、reference pose、骨骼树与骨骼线、Base/Shadow/HitProxy 一致的蒙皮，以及场景保存与重新打开。

本期不做布料、MorphTarget、PhysicsAsset/ragdoll、IK、retarget、AnimGraph/状态机、BlendSpace/Montage、Notify/曲线、骨骼 socket 挂接、LOD 简化、compute Skin Cache 或 GPU 动画求值。这些功能后续从 pose 求值与 vertex factory 边界扩展，不在首版建立空系统。

本文的 DTO（Data Transfer Object）指只保存资产数据的普通结构，例如骨骼名称、父索引、参考变换和动画样本；不包含 World/组件行为或 GPU 句柄。它服务于导入、验证、保存和加载，不表示要新增一种 runtime 对象系统。

骨骼矩阵数组指按本次 draw 的 bone map 排列的蒙皮矩阵数组。它描述数据内容，与用哪一种 GPU 资源存放无关。例如 draw-local bone index 0 通过 bone map 对应 Skeleton 的第 12 根骨骼，shader 便从数组第 0 项读取该骨骼当前的蒙皮变换。

本主题的代码标识符、注释、日志与文档统一使用“骨骼矩阵”“蒙皮矩阵”“法线矩阵”这些直接表达用途的名称，不引入同义术语或隐喻名称。数组和 GPU buffer 分别使用 `bone_matrices`、`bone_matrix_buffer`；需要区分矩阵空间或用途时使用 `component_space_bone_matrices`、`skin_matrices`、`normal_matrices`，类型按同一语义使用 PascalCase，如 `BoneMatrixBuffer`。

## 现有实现与接入依据

| 已核对入口 | 现状与接入影响 |
| --- | --- |
| `engine/core/asset/animation/`、`asset/mesh/skeletal_mesh_asset.*` | 三类骨骼资产保持独立身份，复用基础顶点属性、严格兼容验证和配对事务。 |
| `engine/tools/asset_pipeline/static_mesh_import.cpp` | 已有 FBX/OBJ/glTF/GLB、单位/轴转换及受控文件访问；skin/morph 当前明确拒绝，动画仅诊断忽略。新 importer 复用基础设施，不复用静态网格的层级 bake 语义。 |
| `engine/runtime/rendercore/geometry/vertex_factory.h` | 现有接口只解释顶点流，不选择 shader、材质或 permutation；新增 GPU Skin factory 不能隐式承担这些职责。 |
| `engine/runtime/renderscene/mesh_batch.h`、`view/scene_visibility.cpp` | MeshBatch 使用 PrimitiveSceneProxy、VertexFactory 和 RHI index binding；camera/shadow 统一调用 proxy 的 collect_mesh_batches。 |
| `engine/runtime/renderscene/render_scene.cpp` | 注册、材质更新、最后引用释放通过 PrimitiveSceneProxy 行为调用，具体 geometry 生命周期留各 mesh 实现。 |
| `engine/runtime/rendercore/shader/shader_vertex_input.cpp` | 支持 position/normal/uv/color 和 BlendIndices0/1、BlendWeights0/1；骨骼索引要求 UInt32×4，权重为 Float32×4。 |
| `engine/core/image/pixel_format.h` | RGBA8 UInt/UNorm 分别用于 section-local 索引/权重，与 Core/RHI/Vulkan 映射一致。 |
| compiler `layout/binding_allocator.cpp`、Vulkan buffer view/binding/type mapping | Buffer<Float4> reflection 选择 ReadOnlyTypedBuffer；公共 typed view/limits 验证和 Vulkan uniform texel buffer usage/view/descriptor/保活已接入。 |
| `engine/runtime/gamescene/world/world.cpp` | World 仅在 begin_play 后 tick，当前循环调用 Actor，未提供组件 tick。动画求值时机必须显式接入，不能假定 SkeletalMeshComponent 自动 tick。 |
| `engine/runtime/renderscene/renderer.h`、Editor `assets/thumbnails/thumbnail_preview_scene.*` | Renderer 只有一个现有 thumbnail preview scene/targets；缩略图预览还会修改静态顶点进行归一化。交互骨骼预览需要独立场景，取景应调整相机而非改 skin 数据。 |

沿用 [Assets](assets.md) 的身份/配对事务、[Math](math.md) 的厘米/LH/column-vector、[Render Framework](render-framework.md) 的 FIFO 与 GPU 保活、[RHI](rhi.md) 的 binding/state/profile、[Editor](editor.md) 的候选接管和保存规则。

现有 Assets 文档的 StaticMesh 流程只列 FBX/OBJ，实际 importer 及 CMake 测试还覆盖 glTF/GLB；本方案按真实代码描述已有输入范围，不把这些格式的静态导入支持推断为动画支持。

## 职责、目录与 target

| 层 | 位置 / target | 职责 |
| --- | --- | --- |
| CPU 资产 | `engine/core/asset/animation/`、`engine/core/asset/mesh/` / Toy3dAssets | Skeleton/AnimationSequence DTO、skin 建模数据、SkeletalMesh 渲染数据 DTO、验证与配对格式。 |
| 离线生产 | `engine/tools/asset_pipeline/` / Toy3dAssetPipeline | 源文件解析、坐标归一化、骨骼映射、权重处理、section 构建和动画重采样。Assimp 保持 PRIVATE/可关闭。 |
| 动画播放 | `engine/runtime/animation/` / Toy3dRuntime | 不可变 Skeleton/AnimationSequence、CPU sampler、local/component pose、每实例播放状态。 |
| 游戏侧组件 | `engine/runtime/gamescene/component/`、`actor/` / Toy3dRuntime | SkeletalMeshComponent/SkeletalMeshActor、World 求值阶段、validated settings 与 RT 快照。 |
| 渲染资源 | `engine/runtime/rendercore/geometry/`、`scene/` / Toy3dRuntime | SkeletalMeshRenderData、GPUSkinVertexFactory、SkeletalMeshSceneProxy、每实例骨骼矩阵数组上传。 |
| 帧与 pass | `engine/runtime/renderscene/` / Toy3dRuntime | 公共 MeshBatch、可见性/阴影、program 选择、prepare/execute、预览场景与输出。 |
| 编辑交互 | `engine/editor/source/assets/animation/` / Toy3dEditorCore | 一个关联资产编辑窗口，Skeleton/Mesh/Animation 标签、preview session、导入与错误呈现。 |
| Shader | `engine/shader/include/`、`builtin/` 与 compiler | 共用蒙皮函数、typed Object 资源、Local/GPUSkin permutation、必要顶点语义。 |

不新增 Toy3dAnimation/Toy3dSkeletalMesh 等库，不复制文件/哈希/线程/任务/事务系统。离线算法不依赖 runtime；runtime 的动画业务调度不回移到 Core。

## 三类资产与持久化

| 资产 | 内容 | 依赖与格式提议 |
| --- | --- | --- |
| Skeleton | 唯一 bone name、parent index、reference local TRS；固定父先子后次序、单 root。helper/非 deform 祖先也保留。 | schema 1，YAML；不反向强引用 Mesh/Animation，避免依赖环。 |
| SkeletalMesh | Skeleton 引用、材质槽、LOD0 sections、顶点/索引/skin weights、inverse bind、骨骼局部 bounds。 | schema 1；YAML + `.meta` 的 `skeletal_geometry` 必需段，payload version 2；version 1 按 4 槽读取。 |
| AnimationSequence | Skeleton 引用、duration/sample rate/sample count、track-to-bone 映射与 local TRS 样本。 | schema 1；YAML + `.meta` 的 `animation_tracks` 必需段，段自身 version 1。 |

Skeleton/AnimationSequence runtime 表示为不可变 CPU 数据，可跨 World 共享。Mesh CPU 数据也可共享，RT render data 与可变 pose/骨骼矩阵数组首版按 RenderScene 生命周期隔离；同一 Mesh 的不同组件绝不能共用可变骨骼矩阵数组。

Skeleton 是动画兼容性身份，Mesh 保留自己的 inverse bind；不能以 Skeleton reference pose 推测任意源 mesh 的 bind。首版要求 bone name、parent 和 reference pose 在具名容差内严格兼容：同名但不同父子或不同身体比例不自动匹配，不支持缺骨/增骨合并或 retarget。Mesh 与动画强引用同一 Skeleton，并记录 reference contract 的内容摘要；摘要是失效检查，不能代替 AssetId。

bone index 只对当前 Skeleton 内容有效，重导入按唯一 bone name 重建显式映射，不按旧数组下标接管。重复名字、找不到祖先、环、多 root、缺失 deform bone、非法 bind 或不可表示 TRS 拒绝。Editor 骨骼选择使用 AssetId + bone name，内容变化后重新解析。

复用反射注册/codegen、AssetPairStore、Catalog 依赖检查、AssetRef 和有界 little-endian 编码。禁止直接序列化原生 Matrix/Transform 内存。现有 StaticMesh 格式保持独立；加入 Scene component variant 时提升受影响的 Scene/Component schema，显式迁移旧场景并保留 ID/附着/材质。具体版本以实施时真实 schema 为准。

多资产导入不假设已有跨资产原子事务：先完整验证所有候选，按 Skeleton → Mesh/Animation 逐个配对发布，逐项报告 commit。中途失败可能留下已提交、仍合法的资产；失败不自动删除它们。复用现有资产时检测 ID/内容冲突，不能顺带覆盖被其他 Mesh 使用的 Skeleton。

重导入保持 AssetId；不兼容的 Skeleton 修改先检查依赖并拒绝独立覆盖，用户可选择创建新 Skeleton 后重新导入。兼容候选失败保留旧配对与已显示资源。首版只承诺单资产事务，不新增跨文件组发布协议。

## 可调用入口

- `asset/animation/animation_asset.*`：Skeleton/AnimationSequence 验证、reference hash 与配对编解码；`asset/mesh/skeletal_mesh_asset.*`：skin geometry、bind/bounds 与 Skeleton 兼容验证。三类 description schema 均为 1，动画 payload version 为 1，骨骼几何写 version 2 并兼容读取 version 1；未知版本拒绝。
- `asset_pipeline/skeletal_mesh_builder.*`、`skeletal_mesh_import.*`：构建候选和 FBX/glTF/GLB 源解析，返回 owned CPU 数据，不直接发布资产。Assimp 保持 PRIVATE/可关闭。
- `runtime/animation/animation_pose.*`、`animation_sequence.*`、`sequence_playback_state.*`、`animation_instance.*`：骨骼布局、不可变 sequence、局部采样/混合、独立时钟、Update/Evaluate 与 owned 快照；`animation_player.*` 是单 sequence 的便捷入口。布局和 sequence 构造时复制并完整验证资产候选，多个节点/实例共享 `AnimationSequence`，逐帧仅访问相邻样本。`rendercore/geometry/skeletal_mesh_deformation.*` 单独绑定兼容 Mesh，生成最终 skin/normal 矩阵和 bounds。
- `rendercore/geometry/skeletal_mesh_render_data.*`、`skin_weight_vertex_buffer.*`、`bone_matrix_buffer.*`、`gpu_skin_vertex_factory.*`：geometry 和 section/pose 的资源候选，沿用 RenderResourceManager；ToyGPUSkin.hlsli 是共用 VS LBS 算法。这些资源尚需与组件及生产 shader permutation 连接。

CLI 在 ModelImport 增加 `--skeletal`、`--allow-reduce-influences`、`--sample-rate 30|60`，后两个选项要求同时指定 `--skeletal`。输出目录须已存在，不覆盖已有输出。以 character.asset 为输出时，关联文件为 character_Skeleton.asset、character_Animation_0.asset 等；完整候选编码成功后按依赖顺序逐资产发布，失败报告已提交项。

```powershell
Toy3dModelImport.exe character.glb project/asset/character.asset --skeletal --sample-rate 30
```

真实骨骼 FBX、手机设备、World/Editor 预览尚未完成验收；现有解析入口不能作为这些链路已经可用的依据。

## 导入与 CPU 动画求值

### 源格式与构建

源解析入口包括 FBX/glTF/GLB；OBJ 仍只走 StaticMesh。沿用 Assimp 作为首版 parser，但格式支持必须由真实 skin/bind/动画 fixture 证明，不从“能读 mesh”推导“能正确读动画”。动画单独导入需明确选择已有 Skeleton，source bone 映射/参考关系不能验证时拒绝。

importer 解析与构建 owned 候选，源文件/外部引用受已有 FileSystem 边界约束。单次文件读取最多 64 MiB，累计读取最多 256 MiB / 256 次；node 最多 100000、深度最多 64、mesh 最多 10000、Skeleton 最多 1024 bones、clip 最多 64、单 clip 最长 600 秒，输出动画总样本最多 1000000，网格 vertex/index 各最多 1000000。二进制解码先核对剩余字节对应的数量再分配。第三方 parser 内部临时分配尚无独立总量预算，不能把输入限制描述成完整堆预算。Editor worker/GT 的 generation 复核仍须在接入时实现。

- 源单位和轴只在导入边界统一到厘米、LH、公共 CCW。网格坐标、骨骼 local TRS、inverse bind、root 动画必须使用同一转换，不只转换顶点或平移。
- 将 mesh node 的 bind 变换一致地归一到资产 mesh space；保留骨架层级，不使用静态 mesh 的递归 bake 后丢掉骨骼。
- deform bone 的 helper 祖先进入骨架；无 track 的骨骼/通道补该 Skeleton 的 reference 值。未登记或无法安全映射的动画通道明确诊断，不能错挂到另一骨骼。
- 重复 influence 合并、去零、降序稳定排序并归一化；零有效权重顶点默认拒绝。5～8 个正常接收；超过 8 个时默认拒绝，可由用户显式选择保留最大 8 个，报告受影响顶点数与最大丢弃权重。
- 量化后的有效项紧凑排列；整个 LOD0 最多 4 个有效 influence 时 `num_bone_influences=4`，否则为 8，不按影响数增加 section。权重为 UInt8 UNorm，误差分配后全部槽的整数和精确为 255；索引是 UInt8 section-local bone index，不足补零。CPU 表示容量为 8；payload 与 GPU 顶点流只保存所选 4/8 槽，每顶点 8/16 bytes。CPU bounds 使用最终量化权重，不用量化前数据验证 GPU 行为。
- geometry payload version 2 在嵌套基础网格前保存 UInt32 `num_bone_influences`，每顶点依次保存所选槽数的索引与权重；version 1 无该字段，读取时按 4 槽并补零扩展。新写入统一 version 2；未知版本、非法槽数及 4 槽 CPU 数据中的非零尾项拒绝。迁移仅保留 version 1 解码，不保留第二套构建/渲染路径。
- 一个 draw section 关联一个 material slot 和 bone map，最多 256 bones；超限按三角形确定性切分 section，并复制跨 section 顶点、重映射 index。这个上限不是整个 Skeleton 的上限，Skeleton 总骨骼数另作 CPU/格式预算。
- 暂不导入源材质/纹理，复用现有 slot/override。含 cloth/morph 等未支持功能时明确拒绝，或经用户显式选择忽略并带警告，不能静默成功。

动画首版构建为固定频率、未压缩 local TRS 样本，默认 30 Hz，可改 60 Hz；duration 单独保存，最后样本精确覆盖 clip end，末段按实际时间间隔采样，不能从 sample count 反推近似 duration。源向量插值限 Linear、旋转限最短路径 slerp；根据 Assimp key 的 interpolation 字段拒绝 STEP/CUBICSPLINE，pre/post behavior 限 DEFAULT/CONSTANT。FBX pivot/pre/post rotation 等额外源语义仍需真实 fixture 验收，不从 glTF 的通过推断正确。

### 姿态和蒙皮数学

CPU 动画基础按骨骼布局、局部姿态、播放状态和最终网格派生数据分层。`AnimationBoneLayout` 保存经过验证的 Skeleton 身份、顺序、父索引和参考姿态的不可变副本；首版覆盖完整骨架，不实现 required-bones 子集或 LOD。相同 AssetId 和有序 reference hash 的布局可以混合，骨骼数相同不代表兼容。

`AnimationPose` 只保存布局和 local TRS；支持参考姿态重置、两路和多路非负权重混合。权重归一化，全零权重返回参考姿态；translation/scale 加权，rotation 对齐到最大权重输入的半球后加权并归一化。`ComponentSpacePose` 在最终混合后累积完整矩阵，不分解可能含 shear 的层级结果。`SkeletalMeshDeformationData` 属于 rendercore/geometry，按兼容 Mesh 的 inverse bind 和 bone-local bounds 一次性生成 skin/normal 矩阵与 bounds，不进入动画节点。

`SequencePlaybackState` 是每个播放节点独立的时钟，记录前后时间、实际推进量和跨越循环次数；seek 单独标记，不当成连续播放区间。`AnimationSequence` 构造时复制并完整验证资产数据，可跨节点/实例共享，时钟不进入资源。`AnimationInstance` 每组件/预览 session 独占，持有多路 sequence 状态和混合权重，绑定时验证所有布局和播放参数，失败保留旧实例。Update 接受只读输入快照并推进时钟，Evaluate 不推进时间、不发事件，输出带 revision 的 owned local/component pose 快照；同一次 Update 的重复求值复用缓存，seek、权重/根锁输入和资产替换使缓存失效。首版所有显式配置且处于播放状态的 source 均推进，包括零权重 source；未来状态机负责选择相关节点及其重入/暂停策略。根锁定是混合后的显式预览处理，不参与采样或层级数学。

首版在调用线程（GT/session）执行，实例不可并发访问；输出为不可变共享快照，可交给后续 render 更新。未来状态机基于每节点时钟、贡献权重与中间混合姿态实现重入和中断；转换优先级、每帧转换上限、Notify、RootMotion、curve/attribute、additive 和按骨骼 mask 仍未实现。Update/Evaluate 边界保持独立，为以后通过 TaskGraph 使用不可变输入/owned 输出保留条件，不增加空节点体系或新 target。动画 pose 和播放器不持有 Mesh；验证入口覆盖混合、布局不兼容、循环/seek、缓存、独立实例及最终矩阵/bounds 同帧生成。

CPU sampler 是显式输入 AnimationBoneLayout + AnimationSequenceAsset + sample time、输出绑定布局的 owned local pose 的普通函数，不读 World/UI。公开入口严格验证 clip 兼容性；AnimationInstance 调用不可变 AnimationSequence 的 sample，使用构造时已验证的内部采样入口。sample time 用 double 秒，translation/scale 线性插值，rotation 使用最短路径 quaternion slerp 并归一化；reference/constant track、零时长单样本、clip end、循环 wrap、随机 seek 和非法时间均有独立行为测试。

骨骼支持有限、正 scale 的 TRS；local 不可分解的 shear、镜像/负 scale、零 scale 拒绝。parent 累积使用 Matrix4，合法层级产生的 component affine matrix 不再强行分解为 TRS。允许正非均匀 scale 时，法线需要独立 法线矩阵数组。

column-vector 下，对骨骼 i：

```text
component_pose[i] = component_pose[parent[i]] * to_matrix(local_pose[i])
skin_matrix[i]    = component_pose[i] * inverse_bind[i]
skinned_position  = sum(weight[j] * skin_matrix[bone[j]] * bind_position)
world_position    = component_world * skinned_position
```

root 直接采用 local matrix。归一到同一 mesh space 后，bind pose 必须满足每个有效 skin_matrix 接近 identity；该恒等验证连同已知顶点金值是导入是否正确的核心证据。

GPU 法线采用各 bone skin linear part 的 inverse-transpose 加权后归一化，再经过 component_world 的 inverse-transpose。它是 LBS 法线近似，尤其剧烈形变时不等于变形后三角面重算法线；不宣称等价。退化归一化使用确定的参考方向，包含非有限或奇异矩阵的骨骼矩阵数组在 CPU 发布前拒绝。切线/normal map 不属于现有静态链能力，首版不伪称支持。

root motion 首版不提取、不驱动 Actor：保留 root track，所以 mesh 可能相对 Actor 移动。预览额外提供临时 root lock（只替换 root 的平移/旋转为参考值），不改 AnimationSequence。默认相机固定、不自动追 root；Frame All 显式重取景。

### World 与实例状态（尚未接入）

SkeletalMeshComponent 接入时持有 Mesh、AnimationInstance 和 SkeletalMeshDeformer，通过实例读取 local/component pose 和 pose revision。先完整验证 mesh/skeleton/sequence/settings 候选，再替换；缺资源/不兼容失败保留原实例。单帧播放推进和 pose revision 不改变 World content revision，用户修改资源/播放设置才属于场景内容变更。

播放速率首版为有限正值，不含倒放；Pause 是独立状态。非循环播放到 duration 停止并保持末帧，循环推进到 duration 时 wrap 到起点；显式 seek 可定位闭区间 [0, duration]，seek 到末帧先显示末帧，下一次循环推进再 wrap。随机 seek 不依赖上次采样，零时长 clip 固定显示唯一样本。

拟议接口按领域操作分开：资产 loader 返回不可变 CPU 数据；sampler 接受显式时间并输出 local pose；组件提供 validated mesh/sequence/settings 替换和播放控制；SceneInterface 增加 owned pose/bounds/revision 更新；Renderer 提供独立 animation preview 场景发布/撤回及正常帧输出。具体 C++ 签名在实施前随真实调用链细化，不在本页提供可误认为现有 API 的示例。

建议 World 在 Actor tick 结束后、生成渲染更新前，加一个明确的 skeletal animation evaluation 阶段。注册/注销维护 GT 的 skeletal component 集合，只有 begun_play 的 owner 参与；求值迭代使用稳定快照，删除/退出先撤回注册。首版在 GT 同步求值，复用 sampler，不新增通用 component tick scheduler，也不启动每组件 worker。

编辑 World 不 begin_play、不自动播放。asset preview session 显式推进它自己的播放器；Game/PIE 经 World 阶段播放。暂停/不可见窗口只影响对应 session，不能停主 World。以后并行求值时，将 immutable input/owned output 经 TaskGraph 处理，在 GT 接管后发布 RT，worker 不直接 enqueue_render_command。

## GPU Skin、Shader 与 Mesh Pass

### 顶点与骨骼矩阵数组

GPUSkinVertexFactory 沿用现有 vertex stream validation 的职责，提供 position/normal/UV0/color 加两组 blend indices/weights。BlendIndices0/1 为 UInt32×4（由 RGBA8 UInt fetch 扩展），BlendWeights0/1 为 Float32×4（RGBA8 UNorm fetch）；非法类型和错误 stride/location 不放行。8 槽的索引 offset 为 0/4、权重为 8/12、stride 为 16；4 槽索引/权重 offset 为 0/4、stride 为 8，factory 将第一组重复绑定到第二组逻辑属性，满足相同 shader 输入契约，不额外分配顶点数据。

4/8 共用一个 GPUSkin shader，不增加影响数 permutation 或 ShaderMap key 维度。engine-owned Object UInt32 `toy_num_bone_influences` 由 geometry 提供 4 或 8；shader 先累加四项，值为 8 时再累加后四项，法线只在全部累加后归一化。条件对整个 draw 一致；Base/Shadow/HitProxy 必须复用同一几何档位与 Object 快照。影响数是不可变 geometry 数据，求值时钟、矩阵数组和资源保活沿用原有路径。顶点布局不同仍可产生不同 pipeline 缓存项；共用 shader 不承诺 GPU 对分支读取的具体优化，手机性能须真机验证。

Object 数值成员沿用现有 canonical schema/codegen，新增成员改变 layout hash，已有 shader 产物须重新编译并与生成 C++ 参数共同部署；静态 shader 无需骨骼资源。生产 mesh pass/组件的 Object 快照尚未接入，资源专项使用同一参数编码与 shader 验证 4/8 分支。

骨骼矩阵数组使用 Object group 的只读 **格式化 buffer**，ToyShader 声明为 `Buffer<Float4>`，生成 HLSL 为 `Buffer<float4>`；bones 不使用 StructuredBuffer/SSBO。公共语义是按整数索引读取固定格式元素，不需要 sampler、过滤或 mip。TBO 是 GL/GLES 的命名，不作为公共 RHI 类型名；Vulkan 后端用 uniform texel buffer，D3D 后端用 typed buffer SRV，不能把这些资源直接统称为 GLES TBO。

buffer view format 为 `R32G32B32A32Float`，每 texel 16 bytes；RHIBufferDesc 的 `structure_stride=0`，不能把 texel 大小写进 structured stride。采用正非均匀 scale contract，每 bone 6 个 Float4：3 个 affine position rows + 3 个 inverse-transpose normal rows，共 96 bytes/bone。rows 按标量显式编码，shader 用 dot(row, position/normal)；这是显式 Float4 texel 序列，不改变既有 Matrix4 的 column-major ABI。256 bones 对应 1536 texels、24 KiB，不扩大现有 16 KiB/group uniform contract。

UE 的 bone SRV 数据本身是每 bone 3 个 float4、48 bytes；上述额外 3 个 normal rows 是 Toy3d 支持非均匀 bone scale 的选择，不能描述为 UE 布局。后续缩减布局必须同时改变 scale/法线 contract；资源从 structured 改为 typed 不隐含改变 scale contract。

typed buffer 接入需要贯穿公共 buffer binding 的 typed/structured/raw 区分、Shader reflection/Program layout、RHIBufferView validation、format support、Vulkan usage/view/descriptor materialization 和资源保活。不能从一个 ReadOnlyBuffer 枚举默认推导为 storage buffer，也不能让上层手填 native descriptor type。当前已实现只读 typed 路径，不顺带实现全部 RW/structured buffer 能力。

每 draw 的骨骼矩阵数组按 section bone map 从 component pose 收集；同一实例/section/pose revision 在同帧 Base/Shadow/HitProxy 复用同一 immutable snapshot，不重复采样或按 pass 推进时间。初版用 RT 创建本帧只读 typed buffer + view、通过现有 upload context 拷贝 owned bytes；同帧上传全部在业务 pass 开始前完成。不先引入矩阵上传分配器/skin cache；存在性能证据后再单独优化。

旧骨骼矩阵数组绝不原位覆写。新的 pose 使用新的资源/绑定，list 持旧 RHI refs 到 GPU completion；“双 buffer”或“过了两帧”不能作为复用安全证明。录制 discard 不发布上传 Ready；明确未提交可保留 CPU payload 重录，不确定 submit 进入 terminal，沿用现有错误语义。

### 公共网格边界与 permutation

MeshBatch 使用 PrimitiveSceneProxy、VertexFactory、frame-local geometry draw range/index binding、MaterialRenderProxy 与 Object snapshot；不要求所有网格伪装为 StaticMeshRenderData。不新增只有转发成员的通用 MeshRenderData 基类，持久资源生命周期留各 mesh 实现。

场景收集让各 proxy 输出自己的公共 mesh inputs，准备资源和构造 batch 在 begin_render_pass 前；camera 与 shadow 使用同一入口。RenderScene 注册/材质更新/释放采用真正的 primitive/resource 行为，移除依赖 StaticMesh dynamic_cast 的假通用分支。MeshDrawCommand 保持仅 RHI refs/value/draw args，execute 不识别 skeleton/asset/动画。

尚未接入渲染内部的 Local/GPUSkin vertex factory permutation 维度，由 renderer 自动选择；不是用户材质的播放或 skin 属性。身份必须进入 typed permutation、ShaderMap key、reflection 和加载验证。factory 仍不选择 shader。

compiler 根据该维度注入 engine-owned Object 骨骼矩阵数组 schema 与受控 include；静态 default permutation 不增加骨骼资源要求。shader 作者通过公共 vertex input/deformation helper 获取 mesh-local position/normal，之后沿用 surface 材质逻辑。内置 Unlit/Phong、ShadowDepth 和 HitProxy 共用该 helper，禁止复制三份 skin 公式。项目自定义 shader 必须显式适配公共顶点入口，缺 GPUSkin permutation 时给出不支持诊断，不能自动把任意 HLSL 改写成 skinned shader。

当前 Editor/CMake 主要编译 default permutation；必须增加“当前支持的 mesh shader 所需 Local/GPUSkin 组合”的明确枚举、部署、加载和重编译发布。Material 参数 schema/override 保持跨这两种 factory 一致，允许 vertex input/Object active layout 不同；不能只编译一个 skinned Phong 绕开现有材质系统。重编译完整候选后接管，旧 refs 保持 GPU 生命周期。

### Bounds 与裁剪

不能继续使用 bind-pose AABB 裁剪动画。构建侧为每个参与 influence 的 bone 收集受影响 bind 顶点，经 inverse bind 得到 bone-local AABB；运行时变换这些 bounds 的八角点到当前 component space，再合并、经过 component_world 得 world bounds。

非负归一权重下，实际 LBS 顶点是各 bone 变换点的凸组合，合并 AABB 保守包含它；CPU bounds 运算也以最终量化权重为准；每轴 padding 为 0.01 cm + bounds 最大绝对坐标的 0.00001 倍，非有限结果拒绝。这样不需要每帧 CPU 蒙皮全部顶点，也不靠稀疏采样动画猜最大范围。无 deform influence 的 helper bone 不扩大 mesh bounds，Skeleton-only 预览另按骨骼点取景。

pose 与对应 bounds 以同一 revision 的 owned RT 更新原子接管，不能新 pose 配旧 bounds。离屏但可投影到接收区域的动态 caster 仍进入 shadow 收集；HitProxy 使用同 pose 的蒙皮几何。

## 生命周期、线程、失败与平台

GT 的 World/component/session 是可变状态 owner；RT 的 proxy/render data/骨骼矩阵数组是渲染 owner；RHI list 保留资源至实际 GPU completion。组件注销/窗口关闭先停止推进与新请求，再同 FIFO 撤回 proxy，drain RT 后释放相关 CPU/render owners。Renderer 先撤回 SceneInterface，GPU refs 清理完成后才销毁 device。

新增一条语义明确的 pose 更新桥，payload 包含 owned 骨骼矩阵数组输入、bounds 和 revision；不是传 Component 指针给 RT。资源替换/重导入带 AssetId/content/session generation，GT/RT 都拒绝过期候选。候选失败保留旧 preview；设备 terminal 则停止该渲染域，不能继续显示为新候选已就绪。

| 平台 | 实现评估与必检项 |
| --- | --- |
| Vulkan（当前后端） | 骨骼索引 RGBA8 UInt vertex format；vertex-stage readonly typed buffer 对应 uniform texel buffer。补齐 `UNIFORM_TEXEL_BUFFER` usage、RGBA32F `VkBufferView`、texel-buffer descriptor 与 copy/upload → graphics read 状态/lifetime；view/绑定已可用，mesh pass 接入仍待完成。 |
| 移动 Vulkan profile | 保持 Vulkan 1.1/SPIR-V 1.3，Object 骨骼矩阵数组仍进 physical set 3，总共不超过四 sets；验证 RGBA32F uniform-texel-buffer format feature、max texel elements、view offset alignment 和 vertex-stage sampled-image descriptor 预算。uniform texel buffer 计入 sampled-image 限制，不要求 bone SSBO/storage-buffer descriptor。 |
| D3D11（未实现） | FL11_0/SM5 的 integer vertex fetch 与 VS `Buffer<float4>` typed SRV 路径；Object 逻辑资源映射 stage/register，不借用 UAV、compute 或 D3D11.1-only 常量偏移能力。 |
| D3D12（未实现） | integer vertex fetch、带 RGBA32F format 的 buffer SRV、PSO input layout、upload/resource-state/completion 保活；与 Vulkan 使用同逻辑 schema，无 native 类型上浮。 |

各 API 的格式化 buffer 路径按如下 contract 分别实现，资源模型可实现不代表 Toy3d 已有对应后端闭环：

| API | 资源 / view / binding | 正确性要求 |
| --- | --- | --- |
| Vulkan 1.1 | `VkBuffer` 带 `UNIFORM_TEXEL_BUFFER` 与上传所需 usage；创建 RGBA32F `VkBufferView`，写入 `UNIFORM_TEXEL_BUFFER` descriptor；vertex shader 按元素 Load。 | 使用 `bufferFeatures` 的 uniform texel 格式支持；检查 `maxTexelBufferElements`、`minTexelBufferOffsetAlignment` 及 vertex-stage sampled-image/总资源预算。上传写入到 shader read 的 barrier、view/buffer/descriptor 保活均需实现；不使用 uniform buffer 或 storage buffer descriptor 代替。 |
| D3D11 FL11_0 / SM5 | `ID3D11Buffer`，绑定用途为 ShaderResource，`StructureByteStride=0`；SRV 的 ViewDimension 为 BUFFER，Format 为 `DXGI_FORMAT_R32G32B32A32_FLOAT`，指定 FirstElement/NumElements；绑定 VS SRV。 | 检查 BUFFER/SHADER_LOAD format support、view 范围及 VS SRV 槽预算；没有 structured/raw 标志，不依赖 typed UAV load 或 D3D11.3。上传和 in-flight 保活仍由后端实现。 |
| D3D12 | buffer resource 自身 Format 为 UNKNOWN；SRV 的 ViewDimension 为 BUFFER，Format 为 `DXGI_FORMAT_R32G32B32A32_FLOAT`，StructureByteStride=0、Flags=NONE；通过 descriptor table 绑定 VS。 | typed buffer **不能使用直接 GPU 地址的 root SRV**。检查 format/view 范围/资源预算；保证 upload 完成和 `NON_PIXEL_SHADER_RESOURCE` 读取状态，descriptor 与 backing buffer 均保留至 completion。不将格式写入 buffer resource descriptor。 |
| GLES（仅作边界对照，本期无后端） | Texture Buffer / TBO，按 texelFetch 读取。 | ES3.1 需实际检查 `EXT_texture_buffer` 或 `OES_texture_buffer` 等对应扩展；ES3.2 有 core texture-buffer API。继续核对 format、元素数、对齐及 vertex-stage 限制，不从“手机”或 ES3.1 版本号推断具备 TBO。 |

RHILimits 与 RHIFormatUsage/Vulkan format_capabilities 已表达实际需要的 texel-buffer limits 和 bufferFeatures；不能拿 Texture2D Sampled 支持替代 buffer format 支持。公共 limits/创建 validation 校验 view format、offset/size 的 texel 对齐、device offset alignment、元素数与 vertex-stage descriptor 预算；compiler profile 与 runtime device 都检查 required 集合。不支持的设备明确 Unsupported，不暗中回退 CPU skin。上述 D3D/mobile 是设计可实现性评估，不是已完成后端或已测试结论。

Toy3d 当前移动目标是 Vulkan profile，不是 OpenGL ES 后端；移动支持须按 API/profile/format/limits 表达，不能把“手机”当成一种固定资源能力。UE 还保留 bone uniform buffer 兼容路径，但不表示 Texture Buffer 是所有 OpenGL ES3.1 设备的必选能力。首版不新增 GLES 或 bone UBO fallback；若后续需要该路径，应按实际 uniform 预算重新约束 bones/section，并增加相应编译身份与 schema，不能直接将 24 KiB 骨骼矩阵数组放进现有 16 KiB Object uniform。

## Editor 预览与场景闭环（尚未接入）

建议首版一个 Skeletal/Animation 资产编辑窗口，关联的 Skeleton、Mesh、Sequence 标签共享一个 preview session。窗口能分别打开三类 root_type，不要求打开 Skeleton 时必须有 Mesh。

- Skeleton：骨骼树、父子线/关节点、名称、reference/current transform 只读；选中骨骼高亮，可选兼容 mesh；不编辑骨骼层级或权重。
- SkeletalMesh：reference pose 默认；可选择兼容动画，显示 mesh/mesh+bone/bone-only，材质槽、section bone 数与 influence 信息只读，复用 orbit/pan/zoom/Frame All。
- AnimationSequence：选择兼容 preview mesh 或只看骨架；播放/暂停、循环、倍速、时间轴 seek、逐样本步进、root lock，显示 duration/sample rate。纯预览操作不污染资产/Scene dirty 或 Undo。

预览 renderer 使用正常 Forward/Shadow/Tonemap/UI 路径，骨骼线走一个最小公共 RHI debug line pass；已有 LineList 能力需沿真实 pipeline 验证，不能绕 Vulkan 或临时建第二设备。骨骼 overlay 提供深度遮挡开关，默认 mesh+骨骼可辨识。树选择首版即可定位骨骼，3D bone picking 后续再做；场景组件拾取仍用现有 HitProxy。

建议保留现有 thumbnail scene，增加 Renderer-owned 的一个 animation preview scene/targets；两者同帧调度、共用 device/context/submit，Editor 只持非 owning SceneInterface 和受控 UI texture 身份。一个交互窗口对应一个明确场景，关闭时释放；不顺带设计无限多 preview windows 或另一套 frame scheduler。相机调整以真实厘米 bounds 为依据，不缩放/重写 mesh 顶点、reference pose 或 inverse bind。

SkeletalMesh 缩略图使用 reference pose；Skeleton/AnimationSequence 首版使用类型图标，避免截图依赖可变 preview mesh 或播放时钟。缩略图缓存复用 AssetId/content/generator version 与真实 GPU completion 读回。

runtime 组件 settings 保存 Mesh/Sequence AssetRef、loop/rate/autoplay、primitive flags 和材质 overrides；播放时间/当前 pose/骨骼矩阵数组不保存。闭环包括组件 registry、Place Actor、scene capture/apply/assembly services、DTO/schema/codegen、Undo/Redo、Save/Open，以及独立 PIE render data。引擎资产只读、Editor 写 project/asset、Saved 只放缓存/窗口偏好，所有引用由 Catalog 验证。

## 验证入口

现有专项入口是 Toy3dAnimationTests、Toy3dSkeletalMeshImportTests、Toy3dTypedBufferTests 和 Toy3dTypedBufferVulkanTests，加上 ShaderMap/资源管理/Renderer 所有权回归。真实 Vulkan 专项读取 typed buffer，验证 UInt/UNorm 顶点 fetch、GPUSkin 位移与非均匀 scale 法线、upload discard/retry，以及实际 completion 前后的资源保活；mock 结果不能替代它。

整体功能仍须覆盖下列验收要求：

1. CPU 资产与导入：三类格式与严格兼容验证；验证 bind identity、轴/单位/mesh node、helper 祖先、重复/环/缺骨、4/5/8 influence 量化与档位选择、9 项拒绝/显式削减、旧 4 槽读取、256→257 section 切分、损坏/数量上限、重导入失败保旧、逐资产提交的部分成功。入口使用人工可算的双骨与八项 glTF fixture，真实 FBX 动画仍须验收。
2. CPU 动画与组件：sampler、reference/缺 track、q/-q、clip end/loop/seek/零 duration、正非均匀 scale、root 保留、两个组件独立时间、Actor tick 后求值、暂停/注销。补 schema migration、Scene assembly、Undo/Save/Open。
3. GPU 渲染：先独立完成只读 typed buffer 的公共 validation、Vulkan upload/view/binding/reflection 和真实 VS 读取测试，再迁移 StaticMesh 到公共 MeshBatch 并接 GPUSkin；覆盖错误 buffer kind/format、texel 数量/对齐/descriptor limits、typed permutation/active layout、UInt 输入/format、三类 mesh pass 同姿态、动态 bounds/离屏阴影、ST/MT、discard/submit 失败、旧资源 GPU 保活。CPU 仅作为测试 oracle 计算少量 skinned 顶点，不成为运行时 skin fallback。
4. Editor：三类资产打开、骨架-only、时间轴与 mesh+骨骼、候选加载失败、关闭/重开、thumbnail 与交互预览并存、Engine 资产只读、过期异步、PIE/工程退出；补真实 Vulkan 截图/拾取/阴影与资源反复替换验证。

复用当前 asset/serialization/Shader/RHI/gamescene/Editor 相关测试 target，新增必要专项测试文件并从 CMake 确认条件；实现后主 agent 复查，再交 sub-agent 使用 verify-toy3d-build 独立验证。CMake 改动重新配置和构建受影响目标，交付前通过 `./scripts/format-cpp.ps1 -Changed -Check`。文档不保留验证流水账。

新增公共 mesh 入口后，同批删除旧 StaticMesh-only 的通用调用分支；StaticMesh 专有资源类型继续保留，不加转发头/target 别名。旧 Scene 格式仅保留明确迁移，新增资产格式不兼容未知版本。首版不迁移不存在的 skeleton 数据。

## UE 参考与本项目选择

- [UE4.27 Skeleton Assets](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeleton-assets?application_version=4.27)：借用 Skeleton 关联动画与网格的职责拆分；Toy3d 首版的共享规则更严格，不实现文中 retarget/socket/notify 等能力。
- [UE4.27 Animation Sequences](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-sequences?application_version=4.27)：借用独立 clip、骨骼 TRS track 与 pose sampling 的概念；Toy3d 首版固定频率格式是自己的选择。
- [Epic Skeletal Mesh Rendering Paths](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-rendering-paths-in-unreal-engine)：当前版本资料用于区分 VS GPUSkin、compute Skin Cache，以及 material section 与 bone-budget draw section；不将当前 UE 的平台数字或 Deformer 能力当作 UE4.27 或 Toy3d 基线。
- UE4.27 参考入口 `Engine/Shaders/Private/GpuSkinVertexFactory.ush` 的 BoneMatrices/GetBoneMatrix，及 `Engine/Source/Runtime/Engine/Private/GPUSkinVertexFactory.cpp` 的 FBoneBufferPoolPolicy::CreateResource/UpdateBoneData：bone SRV 为 `Buffer<float4>`，用 RGBA32F typed view，每 bone 3 个 float4；shader/CPU 另有 uniform buffer 分支。特定源码树的 SupportsBonesBufferSRV 返回策略不作为 Toy3d 设备能力依据。
- [Khronos Descriptor Sets](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html)、[Microsoft HLSL Buffer](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-buffer)：typed buffer 的 API 语义和资源预算依据。Vulkan uniform texel buffer 与 uniform buffer、storage buffer 分别验证，不混用 descriptor/format limits。
- [Khronos Uniform Texel Buffer](https://docs.vulkan.org/spec/latest/chapters/descriptors.html)：格式支持成立时，可在所有 shader stages 执行读取；[EXT_texture_buffer](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_buffer.txt)、[OES_texture_buffer](https://registry.khronos.org/OpenGL/extensions/OES/OES_texture_buffer.txt) 与 [ES3.2 规范](https://registry.khronos.org/OpenGL/specs/es/3.2/es_spec_3.2.pdf) 仅用于 GLES 的能力边界。
- [D3D11 Buffer SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_buffer_srv)、[D3D12 Buffer SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_srv)、[D3D12 root descriptor 限制](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-descriptors-directly-in-the-root-signature)：明确 typed view 的元素范围/format/stride 与 D3D12 descriptor table 要求。

采用 Toy3d 的类型/命名、资源与任务系统；不照搬 UObject、反射宏、AnimInstance 对象体系或 UE 调度实现。
