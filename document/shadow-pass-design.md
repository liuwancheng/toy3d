# ShadowPass 实现设计

## 1. 目的与依据

本方案为当前显式 Forward Renderer 增加一盏方向光的实时阴影。第一批在一个 viewport frame 的**同一个 graphics context、同一个 command list** 中，按 `upload → ShadowPass → BasePass → 后处理` 录制；只增加业务 Pass，不引入并行录制、额外 submit、RDG 或通用 Pass Scheduler。后续 pass 级并行另行设计，本方案只保留独立 Pass 输入与资源所有权边界。

本方案须服从 `rhi-design.md`、`rhi-binding-aggregation-design.md`、`shader-system-design.md`、`core-math-design.md`、`material-system-design.md` 及 `../openspec/specs/game-render-framework/view-render-flow/spec.md`。与上述 Active contract 不一致时，以 Active contract 为准并先更新冲突处。

UE4.27 的参考是按视图建立方向光投影阴影、筛选投射物、绘制深度、再供照明阶段使用。Toy3d 借鉴这一数据流，不引入 UE 的阴影图集、屏幕遮罩、缓存或并行绘制体系。光空间方向参考 UE4.27 `DirectionalLightComponent.cpp` 的 `WorldToLight`，深度偏移参考 `ShadowRendering.cpp::UpdateShaderDepthBias()` 与 `ShadowDepthVertexShader.usf::SetShadowDepthOutputs()`；可公开核对的行为见 [UE4.27 CSM 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/dynamic-scene-shadows?application_version=4.27)和[Movable Lights 的 Shadow Biasing](https://dev.epicgames.com/documentation/en-us/unreal-engine/movable-lights?application_version=4.27)。本机源码位于 `D:/ue4.27plus/Engine/Source/Runtime/` 与 `D:/ue4.27plus/Engine/Shaders/Private/`。

## 2. 第一批范围

- 只给一盏方向光生成阴影，也只让这一盏参与方向光照明：从 `enabled=true` 的方向光中选 **`priority` 最高的一盏**，与现有 Forward lighting 的优先级语义一致。同优先级沿用当前 `stable_sort` 的先注册者优先规则，仅作为平局处理。`intensity` 和 `cast_shadows` 不参与选灯；所选灯关闭投影或强度为零时不生成有效阴影，也不改选低优先级灯。其他方向光忽略；现有点光照明保持原有规则，但不生成点光阴影。无可投射物时仍可清出全亮阴影图。
- 每个参与渲染的透视 View 使用近、远两张 `D32Float` 阴影图，每张固定 `1024 × 1024`；受实际 format support、texture limits 与移动端 profile 校验。`Dynamic Shadow Distance` 是从相机沿视线计算的动态阴影终点；零表示禁用阴影。
- 不透明 StaticMesh 投射、接收方向光阴影；先支持已有 Phong 前向着色。Unlit 可以投射，但不接收光照阴影。只有 `MaterialBlendMode::Opaque` 参与；translucent、masked、WPO、skinned mesh、point/spot light 阴影和静态阴影缓存均不在第一批。
- 支持多个 `ViewInfo` 的正确隔离：每个 View 独立计算矩阵、筛选投射物及绑定阴影图；同一 Primitive 在本帧多个 View/Pass 中仍只创建一次 Object binding。`thumbnail_preview_` 明确关闭阴影生成和采样；主场景 Viewport 保持启用。
- 第一批使用同一 Shader 中的 Phong `Forward` Pass 接收阴影，但深度绘制使用独立的内置 `Toy3d/ShadowDepth/Default` mesh shader Program。`Default` 表示不读取材质、可供逐像素覆盖且不修改网格位置的投射物共用的程序；材质仍控制投射资格与双面裁剪。该 Program 不进入 `GlobalShaderMap`。将来支持 masked/WPO 时由材质提供专门的 ShadowDepth Program。

## 3. 当前实现差距与选型

| 现状 | 第一批处理 |
| --- | --- |
| `ForwardSceneRenderer::render_scene_passes()` 只录制 BasePass | 在 View/visibility/typed binding 准备之后插入 `render_shadow_pass()`，随后录制 BasePass |
| `compute_scene_visibility()` 只按相机视锥收集 batch | 增加独立的光空间投射物筛选；画面外但会投影进画面的物体不能被遗漏 |
| Object binding 只覆盖相机可见 batch | 扩大本帧去重集合；键仍为 Primitive identity 与 object-data generation，不建跨帧 cache |
| `MaterialRenderProxy` 只暴露 Forward Program，未暴露 opaque/two-sided 投射策略 | 增加只读的投射资格和有效双面状态查询；不把独立深度 Program 存成第二套材质参数或候选发布入口 |
| `ForwardPassParameters` 只含现有照明值 | 通过 `.shader` schema/codegen 增加阴影矩阵、启用标记、阴影纹理与 普通 Gather sampler；bias 由 ShadowDepth Pass 参数控制，RenderScene 不手工填 binding ID 或 ABI |
| RHI 要求 graphics pipeline 含 pixel shader | 深度 shader 提供无颜色输出的最小 pixel stage，保持现有公共 RHI contract；pipeline 的 color attachment count 为 0 |
| RHI raster state 暂无 depth bias | 第一批在 ShadowDepth 顶点 shader 实现 UE 风格的 constant + slope caster bias，并按 Toy3d reversed-Z 调整符号；接收端 bias 默认零。硬件 raster depth bias 属于后续独立 RHI 设计，不为本 Pass 私自新增后端字段 |
| `SceneRenderTargets` 按 viewport 长期持有颜色/深度 | 扩展 `SceneRenderTargets` 持有 `ShadowRenderTargets`，主场景与 preview 各有自己的实例，不让一次性的 `ForwardSceneRenderer` 持有跨帧纹理 |

`.shader` 的 Pass 参数和资源属于整个 Shader 的共享 schema，不能把 `ShadowDepth` 直接加入 Phong 后让它也被要求绑定正在写入的阴影纹理。独立的默认 ShadowDepth shader 只声明 Object 与光空间 Pass 参数，因此没有 DSV/SRV 自引用，也不要求把 Material binding 伪装成空集合。此决定保持五个逻辑 Binding Group 及 Vulkan 四个 physical set 映射不变。

## 4. 场景数据与选择规则

`DirectionalLightComponent` 增加 `cast_shadows`、有限的 `shadow_distance`、`shadow_distance_fade_fraction`、`shadow_bias` 与 `shadow_slope_bias`；通过现有 `LightSceneData` / `LightSceneProxy` 更新通道传递。`PrimitiveComponent` 增加 `cast_shadows` 与 `receives_shadows`，由 `PrimitiveSceneProxy` 保存，并通过现有 transform/render-state 更新保持 FIFO 一致性。方向光默认关闭阴影，bias 两项默认 `0.5`，距离淡出比例默认 `0.1`，Primitive 默认允许投射与接收；验证场景显式打开方向光阴影。`shadow_distance >= 0` 且有限，淡出比例在 `[0, 1)` 且有限，两个 bias 在 `[0, 1]` 且有限；非法值由 setter 原子拒绝并记录错误。

方向光选择在 `ForwardSceneRenderer` 集中执行一次：从 `RenderScene::lights()` 取出已启用方向光，按 `priority` 降序稳定排序，选排序后的第一盏，保存该 `LightSceneData` 供照明和 ShadowPass 共用。现有 `intensity > 0` 预过滤需从方向光选择中移除；点光仍沿用现有过滤、排序和数量限制。同优先级时 `stable_sort` 保留注册顺序，这是现有代码的平局规则，注册顺序不应盖过不同的 `priority`。若选中灯的 `cast_shadows=false` 或 `intensity=0`，低优先级灯也不能替补；强度为零的灯仍是所选灯，只是照明贡献为零。光的 `direction` 是从光源指向场景的传播方向，Phong 当前用于 `N·L` 的 `scene_light_direction` 取其相反数。超额方向光诊断需说明“仅最高优先级的已启用方向光生效”。

`visible` 是 Primitive 的整体渲染开关；不可见 Primitive 不投射。`cast_shadows` 与 `receives_shadows` 是独立的 Primitive 属性，默认都为 true。前者筛选 ShadowDepth draw，后者由 Object shader 参数 `toy_receives_shadows` 传到 Phong，在 false 时跳过阴影图采样，仍保留方向光、点光和环境光照明。两项通过 RenderScene 的 FIFO Primitive 更新传播；接收开关改变时 Object 数据 generation 必须增加。Details 的 Shadows 分组分别编辑两项，EditorActorState、Undo/Redo 与 `.scene` schema 3 持久化都保存它们。

### Editor 属性与 Scene 持久化

选中 `DirectionalLightActor` 时，Details 的普通灯光区域增加整数 `Priority`（对应现有 `set_render_priority()`）；其下增加可折叠的 **Shadow Map** 分组，包含 `Cast Shadows`、`Dynamic Shadow Distance (m)`、`Distance Fade (%)`、`Shadow Bias`、`Shadow Slope Bias`。距离以引擎米为单位，显示/编辑 `DirectionalLightComponent::shadow_distance`，初始默认值 `100 m`，零值禁用阴影；淡出比例显示为百分数、内部保存 `[0,1)` 的浮点值，默认 `10%`。`Cast Shadows=false` 时其余阴影控件置灰但保留数值；`enabled=false` 时面板仍可修改这些配置，重新启用后按优先级生效。这个分组只显示在方向光上，不把点光的 `Range` 混作方向光阴影距离。

沿用当前 `scene_panels.cpp` 的 ImGui 手势：控件修改进入 `EditorActorState`，经 `apply_actor_state()` 调用 Light setter，手势结束在 `EditorCommandHistory` 合并为一条撤销记录；`capture_actor_state()`、`same_state()` 与 Undo/Redo 都必须包含 `Priority` 和 Shadow Map 字段。先验证所有输入，再修改 Transform/灯光状态，避免半条属性命令生效。Setter 通过现有 `send_render_update()` 发布 `LightSceneData`，预览在下一次正常渲染时更新；距离变动使对应 View 的阴影矩阵和 caster 范围重算，不由 ImGui 直接改 RenderScene 或 RHI 资源。

`.scene` 保存链的 `SceneActorData`、`validate_scene_asset()`、Editor 快照/装配都加入上述方向光和 Primitive 阴影属性。Scene YAML 的 `schema_version` 必须与当前反射类型一致；旧版本由通用资源读取器明确报错，不自动迁移。`Priority` 保存为整数，阴影距离/淡出/bias 按 setter 的有限值与范围规则校验；非方向光的 Shadow Map 字段保持默认值，不作为点光功能入口。

## 5. 光空间与投射物筛选

每个 View 取 `[near_clip, min(camera far_clip, shadow_distance)]` 的有限相机视锥段，记终点为 `effective_shadow_end`；无限远相机使用 `shadow_distance` 作为段终点。若终点不大于 near clip，该 View 的阴影关闭并跳过阴影采样。用 View 的相机参数和有限视锥段计算八个 world-space 角点，不能直接拿无限远投影逆变换的 far 平面角点。此范围只约束阴影图的生成，接收端仍须按相机深度独立限制阴影有效距离，不能把光空间 clip bounds 当作相机距离判断。

### Shadow basis 与稳定投影

UE4.27 的方向光 `WorldToLight` 由光的传播方向构造，CSM 的接收范围来自当前 View 的 split bounds。Toy3d 使用两个 split，但保持相同的职责划分：**光决定轴向，View 决定覆盖区域，投射物决定所需 Z 范围**。不能以某个 caster 的位置或相机朝向作为光轴，也不能每帧用 AABB 主轴重新旋转 basis。以归一化的光线传播方向 `f` 作为光空间 `+Z`；取 `u0 = world +Y`，当 `abs(dot(f,u0)) > 0.99` 时固定改用 `world +X`；`r = normalize(cross(u0,f))`，`u = cross(f,r)`，满足 left-handed `cross(r,u)=f`。退化方向是诊断错误。`world_to_light` 的三行旋转分别为 `r/u/f`，平移为三轴对选定光空间原点的负点积，供 column-vector 左乘使用；方向转动时按同一确定规则重新生成，不在 shader 或后端重新选 basis。

八个视锥角点构成接收段的包围球：中心取角点平均，半径取到中心的最大距离。对固定 FOV、near/far 与 shadow distance，该半径不随相机旋转改变，避免直接拟合光空间 XY AABB 时的尺寸跳变。令 `R` 为阴影图边长，先求 `texel_world = 2*radius/R`，光空间 XY 各增加两个 texel 的 guard；用最终固定半宽 `extent = radius + 2*texel_world` 和 `texel_step = 2*extent/R`，把球心在 `r/u` 轴上的坐标按 `texel_step` 取最近格点。投影的 XY 范围为相对该格点的 `[-extent,+extent]`。这只稳定平移；相机 FOV、阴影距离或光方向变化仍需重算，不承诺无跳变。

遍历场景中 `visible && cast_shadows` 的 StaticMesh world AABB：将八角点投到固定的 `r/u/f` basis；其 XY 与上述 guard 范围相交，且在光线方向上的区间可能位于接收段上游或与接收段相交时纳入候选。候选判定不能仅限相机可见 batch。光空间 Z 近远面同时包住接收段和候选投射物的实际 AABB，再加入具名的有限 padding；光空间原点移至最小 Z 之前，使正交投影的 `0 < near < far` 且所有目标落在范围内。先定候选、再定 Z 范围和 `world_to_shadow_clip`；XY basis/格点不可因候选变化而改变。无法得到有限、非退化的矩阵或 bounds 时，当前 View 返回可诊断错误，不发布不完整阴影状态。

Core Math 增加供真实光空间用例调用的 `OrthographicProjectionDesc` 和 `try_make_orthographic_projection()`：明确 left/right/bottom/top/near/far，检查全部有限及 `left < right`、`bottom < top`、`0 < near < far`；固定 left-handed、column-vector 和 reversed-Z（near=1、far=0），不包含 viewport Y 翻转。`world_to_shadow_clip = projection * light_view`。Shader 将 clip XY 映射到纹理 UV 的方式须与公共 viewport/后端 Y 约定一致，以金值测试和真实角点投影验证，不在 HLSL 中添加按 Vulkan/D3D 分支的翻转。

每个 View 将 `[camera.near_clip, effective_shadow_end]` 分成两个 cascade：split 中心为整段的 `25%`，混合半宽为整段的 `5%`。近级联覆盖 `[near, split + blend]`，远级联覆盖 `[split - blend, end]`；各自独立计算包围球、XY texel snap、投射物集合和 Z 范围。接收端在重叠区按相机深度线性混合两张阴影图。两张纹理分别录制、清除和切换访问状态，仍共用一个 graphics context 与 command list。

## 6. 资源、Pass 与 Shader

`ShadowRenderTargets` 由每个 `SceneRenderTargets` 在 Rendering Thread 持有，按 device、View 数量和每 View 两级联创建或复用纹理；分辨率固定为具名的 `1024`。每张纹理使用 `Texture2D`、单 mip/单 layer、sample count 1、`D32Float`、`DepthStencil | ShaderResource`，创建深度 DSV 与只含 Depth aspect 的 SRV。每次录制均清除该 View 的两张阴影图，即使当前无有效阴影，Phong 仍绑定这两张确定的深度图并以 `shadow_enabled=0` 跳过采样。资源创建不隐式 submit/wait。

`render_shadow_pass()` 只接收已选光源、每 View 每 cascade 的光矩阵、投射物 batch、深度 DSV 及当前 graphics context。它不拥有 viewport frame，不调用 finish/submit/present。每张阴影图在 Pass 前从已发布 access 转到 `DepthStencilWrite`；RHI render pass 无 color attachment，depth load=`Clear`、store=`Store`、clear=0，stencil 不使用；每个 draw 设置独立 pipeline、viewport、scissor、vertex/index buffer、Pass/Object binding。结束后转到 `ShaderResourceGraphics`，供随后的 BasePass 读取。即使没有投射物但方向光有效，也清出确定的全亮阴影图。

内置 `Toy3d/ShadowDepth/Default` shader 的 vertex stage 读取 POSITION0 与 NORMAL0，用 `toy_object_to_world` 和强类型 Pass 参数中的 `shadow_world_to_clip` 输出 `SV_Position`，并在顶点阶段应用下述 bias；pixel stage 无颜色输出。为保证非均匀缩放时的法线正确，公共 Object schema 增加 `toy_object_normal_to_world`（4×4 的逆转置线性部分，平移为零），在 Primitive transform 更新时计算并随 Object binding 一次上传；Phong 的法线变换同时改用此字段。若线性部分不可逆，则记录错误、将该 Primitive 标为不可投射阴影，并为 Phong 填入旧线性变换作为有限兜底值，不得用该值求阴影 bias。Shader Pass 使用 `ColorWrite None`。pipeline 为 TriangleList、Fill、公共 CounterClockwise front face、按 `two_sided` 选择 Back/None cull、depth test/write 开启且 compare=`GreaterEqual`、`color_attachment_count=0`、depth format=`D32Float`。Engine 在 Game Thread 启动阶段通过现有 `ShaderMap` 加载此内置 mesh Program，把 `BuiltinMeshPassPrograms` 交给 Renderer 按值持有；Engine 不保存单独的 ShadowDepth 引用。Rendering Thread 只经 `RHIShaderProgramCache` 创建 RHI 对象，不做 Shader 文件 I/O，也不把它登记为 GlobalShader。后续同类内置 mesh pass 程序加入该集合；材质专用程序仍由材质持有。

### Shadow bias、reversed-Z 与接收端采样

UE4.27 对方向光先按阴影深度范围和 world texel 尺寸缩放 constant bias，再以法线与光方向的夹角计算有上限的 slope bias，在 ShadowDepth 顶点 shader 移动写入深度；它也有独立的 CSM receiver bias。Toy3d 第一批采用**caster constant + slope bias，receiver bias 固定为零**，避免两侧叠加造成阴影悬浮。这是参考 UE 机制后按 Toy3d 坐标、两级联阴影图和 reversed-Z 所作的缩减，不复制 UE 的常量或 bias 符号。

设 `depth_span = far - near`，`depth_per_texel = texel_step/depth_span`（方向光正交投影，使用最终格点的 world texel），`b0 = shadow_bias * 2 * depth_per_texel`，`b1 = shadow_slope_bias * 4 * depth_per_texel`。CPU 将归一化 `f`、`b0`、`b1` 和 bias 上限写入该 View 的 generated `ShadowDepthPassParameters`，每个 caster draw 共用，vertex shader 不重复推导 View 尺度。顶点 world normal 用 `toy_object_normal_to_world` 变换并归一化；`NoL = abs(dot(world_normal, f))`，`slope = min(sqrt(max(0,1-NoL²))/max(NoL,1e-4), 4)`。取 `b = min(b0 + b1*slope, min(5*depth_per_texel, 0.01))`；`4` 是具名最大斜率，`5` 是具名最大 bias texel 数，`0.01` 是具名 normalized-depth 安全上限，实现时作为集中常量而非散落字面值。所有中间量须有限且 `depth_span > 0`。在 clip space 写 `clip.z = max(0, clip.z - b*clip.w)`；正交投影通常 `clip.w=1`，仍保留齐次写法。**减号是 Toy3d reversed-Z 的关键**：caster 被推向更远的 0，接收端使用 `GreaterEqual` 比较；照抄 UE 正向深度的加号会加重自阴影。bias 过大仍会产生 peter-panning，默认值需用斜面、接触处和大/小 shadow distance 的图像测试校准。

Phong `Forward` shader 的 Pass schema 新增两张 `Texture2D<Float>` 阴影图和普通 `Sampler`，以及两个光矩阵、级联混合边界、启用值、`effective_shadow_end` 与 `fade_start`。RenderScene 用 generated `ForwardPassParameters` 和 `create_transient_shader_binding()` 每 View 创建 Pass binding。令接收像素的相机 view-space `z` 为 `d`（left-handed 的前方为正），`d >= effective_shadow_end` 时直接返回完全受光，不采样阴影图；不能仅凭光空间 UV/depth 是否在范围内判断，因为超出相机阴影距离的点仍可能投影到正交阴影图内部。`fade_start = max(near_clip, effective_shadow_end * (1 - shadow_distance_fade_fraction))`；若 `fade_start < effective_shadow_end`，在此区间令 `fade = saturate((d - fade_start)/(effective_shadow_end - fade_start))`，最终可见度 `lerp(shadow_sample, 1, fade)`；淡出比例为零或区间退化时直接硬截断。Toy3d 首批没有静态阴影承接，距离外只有方向光照明而没有方向光阴影。

Sampler 用 RHI 的 `compare_enable=false`、`Nearest`、`ClampToEdge`；从 `world_to_shadow_clip` 求 UV/depth，UV 或深度落在 `[0,1]` 外时直接返回可见，不能依赖边界采样碰巧正确。图内比较为 `receiver_reversed_z >= stored_reversed_z`；默认使用四次无 offset 的 `Gather` 取得 4 × 4 原始深度，手动执行 reversed-Z 比较，再用 UE4.27 的线性 3 × 3 PCF 权重合成；越出图边界的 texel 视为可见，不需要 `shaderImageGatherExtended` 或宏开关。只将阴影因子作用于选中方向光的 diffuse/specular；ambient 与 point lights 不受影响。后续确需 receiver bias 时应单独给出公式与参数，并重新校准 caster bias，不能直接相加旧经验值。

ShadowPass 只处理 `Opaque` 材质。是否 two-sided 应来自材质的有效状态；材质的颜色、纹理、透明度和 shading model 不影响第一批深度结果。缺少可用 Position stream、index range 或 Object binding 的 batch 在录制前诊断并跳过；Shader/PSO、attachment 或 transition 的系统性失败则返回原始 `RHIStatus`，交由帧 owner abort。

## 7. 调用顺序、所有权和失败

一次普通帧维持现有事务边界：

```text
begin_frame
  begin_recording
  record_pending_uploads
  init_views + camera visibility + per-View shadow setup/caster visibility
  create View/Object/Material/Pass bindings（同一 Primitive 的 Object binding 去重）
  render_shadow_pass（逐 View、逐 cascade，depth write → shader read）
  render_base_pass（逐 View 使用对应阴影图与 Pass binding）
  HitProxy / Tonemap / UI / present transition
  finish_recording
end_frame（仍只有一个业务 command list）
```

当前 BasePass 对所有 View 使用一个 `lighting_binding`；必须改为逐 View 的 Pass binding，避免不同 View 误用阴影矩阵和 SRV。Object binding 的创建集合取 camera-visible 与 shadow-caster batch 的并集；同一 Primitive、同一 object-data generation 在本帧至多上传和创建一次。View binding 不冒充 shadow View：阴影顶点变换使用 ShadowPass 自己的 Pass 参数，BasePass 继续使用相机 View binding。

Render Thread 独占可变 RenderScene、MaterialRenderProxy、ShadowRenderTargets 和所有 context 录制。`ShadowRenderTargets` 的 CPU 强引用在 resize/配置变化时可替换；已录制 list 和 backend 延迟销毁必须保活旧资源至 queue completion。只有 `end_frame()` 确认业务 submit 成功后才发布每张阴影图的最终 access；录制失败、abort 或明确未提交时不发布。若 submit 已成功而 present 返回 `OutOfDate`/`Suboptimal`，仍发布资源状态。preview 若有独立 scene 录制，不得借用主 viewport 的正在写入的阴影图。

无灯或灯未投影是正常的功能分支，清空对应 View 的阴影图并将 `shadow_enabled=0`；格式不支持、目标创建失败、shader/pipeline/typed binding 不兼容以及光空间输入退化都返回原始可诊断错误，不用无操作成功掩盖。单个不可绘制 mesh 仍沿用现有 batch 诊断跳过策略；thumbnail 的完整性检查保持当前更严格的行为。

## 8. 跨后端边界

| 语义 | Vulkan 1.1 / 移动端 profile | D3D11 FL11_0 | D3D12 |
| --- | --- | --- | --- |
| 深度写入后采样 | image layout/access barrier，检查 D32 depth+sampled 组合 | DSV/SRV view 与冲突解绑 | depth-write 到 pixel-shader-resource state barrier |
| 无颜色附件深度 Pass | 单 depth attachment 的 render pass | OM 只绑定 DSV，clear depth | 只绑定 DSV，clear depth |
| 空颜色输出 pixel stage | 保留现有必选 pixel shader，fragment stage 无颜色输出 | pixel shader 无颜色输出 | pixel shader 无颜色输出 |
| 深度采样与比较 | `OpImageGather` 原始深度，shader 内 reversed-Z 比较 | `Gather` 原始深度，shader 内 reversed-Z 比较 | `Gather` 原始深度，shader 内 reversed-Z 比较 |

本批不增加 backend 名称分支、公共 RHI 枚举、physical descriptor set 或可选移动端特性。Vulkan ES3.1 profile 的 Cook/runtime 校验须覆盖 `D32Float` usage、raw-depth Gather sampling、纹理大小、shader resource/descriptor 数量；不满足则诊断 `Unsupported`，不得自动提升移动端基线。若实际 target compiler 或某后端不接受无颜色输出 pixel stage，应先做跨后端等价性评估并更新 Active RHI/Shader contract，不能私自塞入伪 color attachment。

## 9. 实施批次与验收

预计直接涉及的文件边界如下；实现时按实际 target 显式登记新增源码和 shader 输入，不将生成头写入源码目录。

| 位置 | 具体职责 |
| --- | --- |
| `engine/core/math/matrix_construction.*` | 正交 reversed-Z 投影构造与输入校验 |
| `engine/runtime/gamescene/component/light_component.*`、`primitive_component.*` | 投射开关、方向光距离/淡出/bias 的 Game Thread 配置与 RenderCommand 更新 |
| `engine/runtime/rendercore/scene/light_scene_proxy.h`、`primitive_scene_proxy.*`，`engine/shader/builtin_shader_parameters.h` | Render Thread 场景值、Object 法线矩阵/有效性与共享 schema；不持有 RHI 对象 |
| `engine/runtime/renderscene/view/scene_visibility.*` 与新的 `pass/shadow_pass.*` | 光空间候选筛选、StaticMesh batch 构建、深度 Pass 录制；RenderScene 的 Primitive 枚举只通过窄的 scene visibility 入口访问 |
| `engine/runtime/renderscene/object_shader_bindings.*`、`view/forward_scene_renderer.*`、`pass/base_pass.*` | 跨 Pass 的 Object binding 去重、逐 View Pass binding、固定录制顺序 |
| 新的 `engine/runtime/renderscene/shadow_render_targets.*` 与 `renderer.*` | 阴影图资源所有权、bootstrap、提交后 access 发布和延迟销毁 |
| 新的 `engine/shader/builtin/shadow_depth/default.shader` 与 `engine/shader/builtin/surface/phong.shader` | 深度顶点/无颜色像素程序及方向光阴影接收 |
| `engine/runtime/engine.cpp` 与 Shader/Runtime CMake target | Game Thread 预载内置阴影 Program、显式注入和构建登记 |
| `engine/editor/source/panels/scene_panels.cpp`、`placement/actor_factory.*`、`commands/editor_command_history.cpp` | 方向光 Priority/Shadow Map 控件、属性快照、撤销重做与 setter 更新 |
| `engine/core/scene_asset/scene_asset_data.h`、`scene_asset.cpp`、`engine/editor/source/editor.cpp` | Scene 字段验证、当前 schema 保存/打开回填 |

1. **数学与场景值**：正交 reversed-Z 构造及金值测试；`+Z`、近垂直方向的 basis 正交性/左右手性与 world-to-light 点投影；固定视锥参数下相机平移一格 texel 前后的矩阵稳定性。Light/Primitive 的 `cast_shadows` 更新与非法值测试；方向光依次覆盖“较高优先级灯禁用、较高优先级灯不投影、较高优先级灯强度为零、后注册灯优先级更高、同优先级稳定平局”，验证照明和阴影始终选择同一盏最高优先级的已启用灯。有限阴影距离与非均匀/奇异 transform 也要覆盖；Details 改动距离/淡出/优先级的实时效果、撤销重做、当前 `.scene` schema 往返保存及旧 schema 拒绝分别验收。
2. **Shader 与目标**：内置 opaque 阴影 shader、Phong 强类型阴影资源、普通 Gather sampler；深度 DSV/SRV 与 format capability 校验。验证 generated 参数、reflection、shader compile、无颜色深度 pipeline。
3. **可见性与共享绑定**：光空间 caster 收集覆盖画面外投射物；Object binding 在 Base/Shadow 间及多 View 间按 Primitive/generation 复用；阴影 batch 不要求 Material binding。
4. **帧闭环**：`render_shadow_pass()` 插入单 context 顺序，BasePass 改用逐 View Pass binding；保持现有 finish/end_frame/abort 事务。覆盖 resize、多 frame-in-flight、submit 成功但 present 失败的状态发布。
5. **图像与性能验证**：固定相机/光源/网格的阴影方向、reversed-Z 比较及 bias 正负号金值、离屏投射物、无灯全亮、two-sided、PCF、斜面 acne 与接触处 peter-panning、移动相机稳定性；在 `fade_start` 前、淡出区间内、恰好及超过 `effective_shadow_end` 的接收点验证阴影可见度，并覆盖“相机超距但光空间仍在图内”的情况。运行 Vulkan validation，并观察阴影图分辨率与每 View draw 数。CMake 变更完成配置和受影响目标构建，代码实现完成后由独立验证者运行构建和测试。

验收完成条件：方向光打开后，主 viewport 能在动态 StaticMesh 上显示稳定阴影；无方向光、关闭投影或 thumbnail 仍正常绘制；全帧只有一个业务 command list；无 Vulkan validation error；失败帧不提交阴影资源的错误 access，也不丢失待重录上传。性能数字只记录在实施/PR 验证中，不写回长期设计 contract。

## 10. 后续边界

并行录制将在单 list 阴影闭环验收后另行设计多 list submit、command-list 状态衔接、同帧上传跨 list 可用性和每 context 独占 pool。该阶段才拆分 Pass 的 prepare 与 record，并用 Task Graph 将 `ShadowPass`、`BasePass` 分配给不同 context。更多 cascade、masked/WPO、点/聚光灯阴影、硬件 raster depth bias、阴影缓存和 RDG 都是独立后续能力，不作为第一批完成条件。
