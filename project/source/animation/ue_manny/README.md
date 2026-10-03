# UE Manny 动画测试源资源

来源为本机 UE 5.5.3 的第三人称模板 `Templates/TemplateResources/High/Characters/Content/Mannequins`，版权属于 Epic Games，用于仓库所有者的个人本机测试。原始 `.uasset` 不修改、不保存到 Toy3d；此目录保存 UE 自带 exporter 的输出。

## 文件与用途

| 文件 | 用途 |
| --- | --- |
| `SKM_Manny.fbx` | 完整 Manny 的 LOD0、蒙皮权重、骨架、参考姿态与 bind 数据。 |
| `SKM_Manny_Simple.fbx` | 简化版本，导出骨架有 89 根骨骼，作为另一套网格/骨骼映射测试输入；不能与完整 161 骨骼网格直接视为同一导入布局。 |
| `SK_Mannequin.reference.json` | UE Skeleton 的 161 根骨骼名称、父索引、参考 local TRS。 |
| `MM_Idle.fbx` | 待机。 |
| `MM_Walk_Fwd.fbx`、`MM_Walk_InPlace.fbx` | 行走与原地行走。 |
| `MM_Run_Fwd.fbx` | 奔跑。 |
| `MM_Jump.fbx`、`MM_Fall_Loop.fbx`、`MM_Land.fbx` | 起跳、下落、落地。 |
| `MM_T_Pose.fbx` | T Pose 动作；不能替代网格自己的 bind pose。 |
| `MM_*.samples.json` | 每个动作在 0、duration/2、duration 的 UE source local pose，作为后续采样核对输入。 |
| `manifest.json` | UE 版本、资产路径、导出选项、动作时长/帧数、原始资产及 FBX 的 SHA256。 |

FBX 使用二进制 2013 格式，只导出 LOD0，不导出 MorphTarget、碰撞或烘焙材质。每个动作 FBX 都携带同一个完整 Manny 预览网格，使当前要求带网格的 skeletal importer 能检查 geometry/bind/animation；目录没有独立导出的纹理，也没有移植 UE 的 Control Rig、AnimBlueprint、IK、物理或布料系统。

JSON 姿态保持 **UE 的厘米、LH、Z-up、parent-local**，quaternion 为 xyzw；它们是核对用的源数据，不能直接当 Toy3d runtime pose 或 `.asset` 加载。FBX 由 UE exporter 写入自己的轴/单位 metadata，Toy3d 必须在导入边界对顶点、local pose、inverse bind 和动画统一转换。

## 重新导出

在仓库根目录运行：

```powershell
./scripts/export-ue-manny.ps1 -UnrealRoot D:/GitProject/ue5.5.3
```

可用 `-OutputDirectory` 指定输出位置。临时 UE 工程、资产副本、缓存和日志位于根 `build/verification/ue-manny-export`；调用 UE 的 Python commandlet，不要求系统 Python 安装 Unreal 模块。UE 5.5 的骨骼 FBX 导出依赖渲染组件，使用 `AllowCommandletRendering` 和 D3D11，不能切换为 `NullRHI`。

## Toy3d 测试资产

正式运行时资源位于 [project/asset/animation/ue_manny](../../../asset/animation/ue_manny)。完整 Manny 和 8 个动作共用 `SKM_Manny_Skeleton.asset`；Simple Manny 使用自己的 `SKM_Manny_Simple_Skeleton.asset`。不携带 UE 材质/纹理，材质由 Toy3d 指定。

向一个已存在且为空的输出目录导入时，先创建 mesh/Skeleton，再逐个复用已有 Skeleton 导入动作：

```powershell
build/engine/tools/asset_pipeline/Debug/Toy3dModelImport.exe project/source/animation/ue_manny/SKM_Manny.fbx OUTPUT/SKM_Manny.asset --skeletal
build/engine/tools/asset_pipeline/Debug/Toy3dModelImport.exe project/source/animation/ue_manny/MM_Idle.fbx OUTPUT/MM_Idle.asset --skeletal --skeleton OUTPUT/SKM_Manny_Skeleton.asset --animation-only
```

其余动作同样传入已有 Skeleton。输出不覆盖旧资产；重新生成应使用新目录。`--animation-only` 当前仍通过 FBX 内的 preview mesh 检查绑定关系，只发布一个 AnimationSequence。

`Toy3dRuntime.MannyAnimation` 读取正式资源，使用 UE reference 和 24 个 source pose 快照核对 CPU 链路；`Toy3dAsset.SkeletalMeshImport` 覆盖源节点重名、布局重映射及不兼容拒绝。源资源导出或 CPU 核对成功，不表示 Editor/GPU/手机链路均已验收。

UE 导出的网格组节点与网格节点可以同名，例如 `SKM_Manny` 的 Null 和 Mesh 节点；骨骼名称仍然唯一。importer 按节点身份记录普通层级，骨骼/动画通道名称仍严格唯一，并排除顶端 geometry 导出容器；源 FBX 不做改写。
