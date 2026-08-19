# Toy3d 渲染引擎基础架构与执行计划

## 1. 文档状态

本文定义 Toy3d 在不引入 RDG 的前提下，完成第一版可运行渲染引擎所采用的长期边界和分阶段执行计划。本文覆盖 GameScene、RenderScene、Game/Render 双线程、Forward Renderer、Material、显式 Pass、PostProcess、ImGui 以及 Editor 演进路径。

本文中的架构决策已经逐项确认。实现阶段若发现公共 RHI 或共享基础设施缺口，应回到对应模块补齐设计与测试，不得在 RenderScene 内建立后端旁路、通用临时 Scheduler 或重复基础设施。

相关文档的职责如下：

- `rhi-design.md` 定义公共 RHI 与后端边界；
- `shader-system-design.md` 定义 Shader 资产、编译、反射、ShaderMap 与 Binding ABI；
- `rhi-binding-aggregation-design.md` 定义五个逻辑 Binding Group 的跨后端聚合；
- 本文定义上述能力如何组成完整的 Scene Renderer、线程模型与 Editor-ready 帧模型。

## 2. 目标与非目标

### 2.1 第一里程碑目标

第一里程碑在 Windows Vulkan 上完成以下可运行闭环：

- Main Thread 即 Game Thread；
- Game Thread 与 Render Thread 可独立运行；
- 可通过启动配置关闭多线程，关闭后仍保持相同 RHI/RenderScene 所有权边界；
- 默认允许 Game Thread 比 Render Thread 领先一帧；
- 持久 `World` 与持久 `RenderScene` 通过增量消息同步；
- 使用 Phong 光照渲染多个自旋转 Cube；
- 支持 Directional、Point 与 Spot Light；
- Point 与 Spot 共享最多八个 Forward local-light 槽位；
- 采用 Forward、Tone Mapping、Final Composition 与 ImGui 的显式 Pass 序列；
- 支持 Opaque 与受限普通 Translucent；
- ImGui 可作为 runtime overlay 和未来 Editor 的共同 UI 基础；
- resize、最小化、失败帧、flush 与 shutdown 不产生死锁或资源越界。

### 2.2 非目标

第一里程碑不实现：

- RDG 或临时通用 Pass Scheduler；
- Deferred Renderer；
- 实时阴影；
- RHI Thread；
- Pass recording worker、单 Pass draw-range 并行或通用 Task System；
- async compute、multi-queue、ray tracing、VRS、bindless 或多 GPU；
- tiled/clustered lighting 或 per-object light list；
- PBR、Material Graph、runtime shader compilation；
- normal map、emissive、alpha mask、refraction、OIT 或半透明阴影；
- LOD、skeletal mesh、streaming、instancing 或 draw merging；
- ImGui Multi-Viewport、原生 IME composition 或 Gamepad Navigation；
- D3D11、D3D12、Android 与 macOS 的实际运行验收。

以上能力后置不表示公共接口可以围绕 Vulkan 桌面路径设计。新增 RHI contract 仍须评估 Vulkan、D3D11 FL11_0/SM5、D3D12 与 `VulkanPortable v1`。

## 3. 总体分层

```text
Main / Game Thread

World（GameScene）
├── Actor / SceneComponent
├── StaticMeshComponent / CameraComponent
├── Directional / Point / Spot LightComponent
├── Material / MaterialInstance references
└── SceneViewFamily / SceneView 构建
        │
        │ owned value + stable ID
        v
RenderFramePacket
        │
        v
Render Thread

RenderScene（持久镜像）
├── PrimitiveSceneInfo / PrimitiveSceneProxy
├── LightSceneInfo / LightSceneProxy
└── RenderResourceCache references
        │
        v
ForwardSceneRenderer（每个 ViewFamily 临时创建）
├── visibility / light selection
├── MeshBatch → ForwardMeshProcessor → MeshDrawPacket
├── PreparedRenderFrame
└── explicit pass recording
        │
        v
公共 RHI → Vulkan / D3D11 / D3D12 backend
```

边界原则：

- `World` 只属于 Game Thread；
- 多线程模式下，`RenderScene`、RHI device、RHI viewport 与所有 RHI 对象只由 Render Thread 访问；
- 跨线程不捕获 Game 对象或 Proxy 裸指针；
- 上层业务对象不手工管理 Render ID；
- 单线程模式下，上述对象只在 Main Thread 的 `RenderFrameDispatcher` 调用链中访问；Gameplay
  仍不得直接访问 RHI；
- RenderScene 不依赖 Vulkan、D3D11 或 D3D12 原生类型。

## 4. 模块落点

建议按现有仓库职责落位：

```text
engine/runtime/gamescene/
├── world / actor
├── components/
├── transform hierarchy
└── render update collection

engine/runtime/rendercore/
├── shader/                       # 已有 ShaderMap runtime
├── material/                     # Material / MaterialInstance
└── geometry/                     # StaticMesh / Section / Slot CPU contract

engine/runtime/renderscene/
├── render_thread.*
├── render_frame_packet.*
├── render_frame_completion.*
├── render_frame_queue.*
├── scene/                        # RenderScene、Info、Proxy
├── resources/                    # RenderResourceCache
├── view/                         # ViewFamily / View / visibility
└── renderer/                     # Forward、MeshProcessor、Pass

engine/runtime/ui/
├── imgui_platform_bridge.*
├── imgui_draw_packet.*
└── imgui_texture_registry contract

engine/editor/
└── Editor widgets、Docking layout 与 Editor policy
```

本轮不在 `engine/core/` 新建 Thread、Event、Fence、Queue 或 Task System。`RenderFrameQueue` 与 `RenderFrameCompletion` 的 contract 直接依赖渲染帧、RHI shutdown 和 one-frame lag，属于渲染领域机制。后续 Pass 并行需要通用 `TaskSystem` 时，必须先单独形成 `engine/core/` 基础设施设计文档。

## 5. GameScene 领域模型

### 5.1 最小对象集合

第一版只建立精简 Actor/Component 模型：

- `World`；
- `Actor`；
- `SceneComponent`；
- `StaticMeshComponent`；
- `CameraComponent`；
- `DirectionalLightComponent`；
- `PointLightComponent`；
- `SpotLightComponent`。

不引入 UObject、GC、反射或完整 ECS。对象使用 C++17 RAII；独占所有权优先 `std::unique_ptr`，只有不可变资产共享和明确共享生命周期才使用 `std::shared_ptr`。

### 5.2 Transform hierarchy

`Actor` 最多有一个 `RootComponent`。每个 `SceneComponent` 保存 local translation、rotation、scale 与 parent 关系。attachment 可以跨 Actor，但父子必须属于同一个 `World`；组件所有权不随 attachment 转移。

矩阵约定继承全引擎 contract：left-handed、+X right、+Y up、+Z forward、column vector、column-major storage。组合顺序固定为：

```text
world_matrix = parent_world_matrix * local_matrix
```

第一版缩放约束：

- 每个轴必须满足 `scale > epsilon`；
- 支持 positive non-uniform scale；
- 不支持零缩放和负缩放；
- 非法缩放必须可诊断，不能静默取绝对值或钳制为正数；
- normal matrix 使用 world linear transform 的 inverse-transpose；
- world bounds 由 local bounds 经 world transform 得到。

Transform 或 attachment 改变后，当前节点与所有后代标记 dirty。`World` 在帧末按父子顺序更新 world transform 与 world bounds。`attach_to()` 必须检查同 World、重复挂接与 cycle；`KeepWorld` 在 parent world transform 不可逆时失败。Render Thread只接收最终 world transform 和 world bounds，不维护 Game 侧 hierarchy。

### 5.3 Camera

第一版只实现有限远平面的 Perspective Camera：

```text
vertical_fov = 60 degrees（默认）
near_clip    = 0.1 m（默认）
far_clip     = 1000 m（默认）
aspect       = SceneView ViewRect
clip depth   = 0..1 reversed-Z
```

FOV、near 与 far 都由 `CameraComponent` 配置。接口预留 infinite-far perspective、Orthographic 与高级自定义 projection，但自定义 projection 不得悄悄破坏引擎的 depth 与坐标约定。

## 6. Render ID 与跨线程协议

### 6.1 稳定 ID

跨线程实体使用引擎内部强类型 64 位 ID：

- `0` 为 invalid；
- 进程生命周期内不复用；
- `PrimitiveId`、`LightId`、`RenderSceneId`、`ViewportId`、`SceneOutputId` 与各类资源 ID 不得混用；
- ID 由引擎在注册边界自动分配，Gameplay 上层不传递或维护 ID；
- Component 离开 World 后重新注册时分配新 ID，不复活旧 ID。

ID 解决跨线程身份，owned value payload 解决跨线程数据所有权。二者不能由裸指针捕获替代。

### 6.2 Dirty 分类

采用三类 Render dirty：

- `RenderTransformDirty`；
- `RenderStateDirty`；
- `RenderDynamicDataDirty`。

MaterialInstance revision 由独立资源更新收集器处理，不为每个引用该 Material 的 Primitive 重复生成 DynamicData 更新。

### 6.3 帧末合并

同一 ID 在一个 `RenderSceneUpdateBatch` 中最多出现一次，合并优先级为：

```text
Remove > Add/FullState > State > Transform + DynamicData
```

规则：

- 新增后多次修改只发送最终完整 `Add`；
- 同一 Tick 新增又销毁不发送消息，已分配 ID 仍作废；
- 已存在对象销毁时 `Remove` 覆盖此前 dirty；
- `RenderStateDirty` 发送完整 snapshot，Render Thread 可保持 ID 并重建 Proxy；
- Transform 与 DynamicData 可合并在一条 bitmask update 中；
- payload 全部拥有其数据，Apply 后不回访 World 或 Component；
- duplicate Add、unknown Update 等协议错误必须诊断，不能静默创建或覆盖。

## 7. World、RenderScene 与 View

### 7.1 持久 Scene 镜像

正式关系为：

```text
World（Game Thread）
    ↕ ID / 增量更新
RenderScene（Render Thread）
    ← SceneViewFamily / SceneView（每帧观察数据）
```

一个 `World` 对应一个持久 `RenderScene`。一个 `RenderScene` 可以被多个 View 观察。每帧不复制整个 World；Render Thread按 batch增量维护 Primitive 与 Light 镜像。

### 7.2 SceneViewFamily 与 SceneView

`SceneViewFamily` 表示共享 RenderScene、输出目标与渲染配置的一次观察请求；内部接口使用 `Views[]`。一个 Family 内多 View用于 stereo 或 split-screen。不同 World、不同输出或不同 Editor Scene Viewport 使用不同 Family。

第一版数据接口支持 `Views[]`，实际执行只允许一个 View；收到多个 View 时返回明确 `Unsupported`，不能静默只渲染第一个。

### 7.3 ViewportFrame 与 SceneOutput

```cpp
enum class SceneOutputType
{
    Present,
    Offscreen
};

struct SceneOutput
{
    SceneOutputId output_id;
    SceneOutputType type;
    Extent extent;
};

struct SceneViewFamilyFrame
{
    SceneViewFamily view_family;
    SceneOutput output;
};

struct ViewportFrame
{
    ViewportId viewport_id;
    std::vector<SceneViewFamilyFrame> scene_frames;
    std::shared_ptr<const ImGuiDrawPacket> imgui;
};
```

一个 `ViewportFrame` 可包含多个 Offscreen family，最多一个 Present family，也可以没有 Scene、只显示 UI。同一帧不能有两个 Family 写同一个 `SceneOutputId`。

第一里程碑实际只实现一个 native viewport、一个 Present family 与一个 View。数据模型从第一版保留多 Family/多输出能力，避免 EditorWorld 与 PlayWorld 接入时破坏 packet ABI。

### 7.4 持久 Offscreen 输出

Offscreen output 由稳定 `SceneOutputId` 标识，并在 Render Thread持有版本化 `SceneOutputResource`。create、resize 与 release 都通过 Render 消息执行。resize 产生新版本，旧 texture 由 Prepared Frame与 RHI frame slot 保活至 GPU 安全。

`extent == 0` 时跳过输出。第一版 Editor-ready 格式为 Tone Mapping 后的 linear SDR `RGBA8_UNorm`。同一 packet 中先写 Offscreen、transition 为 ShaderResource，再由主窗口 ImGui 通过稳定 `ImGuiTextureId`采样；不把 RHI texture 指针传回 Game Thread。

## 8. Render Thread 与帧同步

### 8.1 线程所有权

多线程模式由 Render Thread 独占；single-thread fallback 由 Main Thread 在
`RenderFrameDispatcher` 调用链中独占：

- `RHIDevice` 与所有 RHI 对象；
- `RHIViewportContext`；
- `RenderResourceCache`；
- 所有 `RenderScene`；
- viewport/output render resources；
- Scene Renderer 的 Apply、Prepare、Record、Submit 与 Present。

OS Window、Input、World、ImGui Context 与 Widget 构建只属于 Main/Game Thread。

### 8.2 聚合 RenderFramePacket

每个 Game Tick只提交一个全局 packet：

```cpp
struct RenderFramePacket
{
    RenderFrameId frame_id;
    FrameTiming timing;
    std::vector<RenderResourceUpdate> resource_updates;
    std::vector<RenderSceneUpdateBatch> scene_updates;
    std::vector<ViewportFrame> viewport_frames;
};
```

一个 packet可同时包含 EditorWorld、PlayWorld 与多个 viewport。Render Thread固定按以下顺序处理：

1. Apply resource updates；
2. Apply all scene updates；
3. Prepare all ViewFamilies；
4. record uploads and passes；
5. submit and present；
6. complete frame completion；
7. 才处理下一个 packet。

同一 packet 的 Apply 到 Submit 不与下一 packet交错，因此不需要公开 `freeze()/unfreeze()`。`PreparedRenderFrame` 必须只读并强持有资源；录制期间可使用内部 debug counter/assert 发现非法写，但不把 Freeze 变成架构 API。

### 8.3 one-frame lag

帧节流采用 UE 风格双 completion 轮转：

- 默认 `one_frame_thread_lag=true`；
- Game Thread提交 N 后等待 N-1；
- lag关闭时提交 N 后等待 N；
- 第一个 lag帧没有 N-1，只提交不等待；
- 队列最多包含一个 processing 与一个 queued packet；
- Render completion 表示 worker（未来）、RHI submit 与 present调用已结束，不表示 GPU 已执行完毕；
- GPU completion 由 RHI frame slot、queue completion value 与 backend fence管理。

单线程模式不创建 Render Thread，强制 lag关闭，并在 Main Thread同步执行相同渲染管线阶段。Gameplay 仍不得直接访问 RHI。

### 8.4 Main Thread帧顺序

```text
1. Pump OS events
2. 更新 Input
3. Tick World / Actor / Component
4. 构建 ImGui widgets并确定 Editor viewport尺寸
5. 更新 Transform hierarchy与 world bounds
6. 构建 SceneViewFamily / SceneView
7. ImGui::Render()并深拷贝 ImGuiDrawPacket
8. 收集资源更新与 RenderSceneUpdateBatch
9. enqueue RenderFramePacket(N)
10. 按 lag策略等待 completion
```

先布局 ImGui再构建 SceneView，使未来 Editor 可按中央面板的真实像素尺寸创建 Offscreen输出。

### 8.5 初始化与关闭

初始化使用同步 completion握手：

1. Main Thread创建 OS Window与 surface descriptor；
2. 启动 `RenderFrameDispatcher`；
3. 渲染执行线程创建 RHI device、viewport、RenderResourceCache；
4. bootstrap上传 Error Material、placeholder textures与 ImGui font；
5. bootstrap submit完成后才开放正常帧。

shutdown顺序：

1. Main Thread停止提交新 packet；
2. 等待所有 frame completion；
3. 等待未来 recording workers退出；
4. 渲染执行线程销毁 viewport/output resources、RenderScene与 cache；
5. 等 GPU/RHI shutdown所需完成点并销毁 device；
6. join Render Thread；
7. Main Thread最后销毁 OS Window。

窗口关闭事件只设置退出请求，不立即销毁 native window。所有失败路径必须完成正在等待的 completion。

## 9. RenderScene 数据模型

### 9.1 Primitive

每个可绘制组件在 Render Thread对应：

```text
PrimitiveSceneInfo
└── PrimitiveSceneProxy
```

Proxy由 Render Thread创建、拥有和销毁。Proxy只生成 `MeshBatch`，不得直接录制 RHI draw。`PrimitiveSceneInfo` 保存 RenderScene索引、bounds、visibility相关状态与 Proxy所有权。

### 9.2 Light

Light使用独立模型：

```text
LightSceneInfo
└── LightSceneProxy
```

Directional、Point 与 Spot不得塞进 Primitive路径。Light Proxy保存 Render Thread所需的只读光源参数与 bounds。

### 9.3 生命周期错误

对已存在 ID执行 Add、对未知 ID执行 Update，以及重复 Remove均为同步协议错误。内容级错误可跳过对象，但不得破坏 batch后续对象的 Apply。Proxy重建失败时保留错误诊断并按资源重要性选择 placeholder或跳过，不能留下半更新对象。

## 10. RenderResourceCache

### 10.1 作用域与版本

`RenderResourceCache` 是 Device/Render Thread级共享对象，RenderScene只持所用资源的强引用。Mesh、Material、Texture使用各自强类型 Render Resource ID与单调 revision。

更新规则：

- 只接受高于 current revision的新版本；
- 旧 revision忽略并诊断；
- 相同 ID、相同 revision但内容不同是逻辑错误；
- 新版本不可变，替换 latest entry不原地修改旧 GPU资源；
- RenderScene与 Prepared Frame持有具体版本；
- 最终 GPU安全销毁由 RHI deferred-deletion/frame-slot机制完成。

资源只允许在 Render Thread Apply/Prepare阶段 resolve。Recording worker不得触发 cache miss创建、文件 I/O、shader编译或 GPU upload。

### 10.2 miss 与失败

- 缺少 Material或 Material shader：使用 Error Material；
- 缺少 Texture：按语义使用 checkerboard、white或 normal placeholder；
- 缺少 Mesh vertex/index data：跳过 Primitive并诊断；
- SceneColor、SceneDepth、Forward shader、viewport等帧级必需资源失败：abort当前帧；
- 第一版使用显式 `ReleaseResource` 移除 latest entry，不实现 LRU、streaming或预算驱逐。

### 10.3 随帧上传

Procedural Cube在 Game侧表现为普通 immutable `StaticMesh` CPU资产。资源更新 payload拥有 vertex/index bytes与 Section metadata。

Render Thread Apply创建 GPU buffer并标记 `PendingUpload`。当前帧在所有图形 Pass前统一录制 upload与 transition；同一 submission中 upload排在消费它的 Forward draw之前，因此第一帧即可显示且无需 CPU等待。

禁止资源创建函数内部隐式 `submit()`、`wait_idle()`或创建一次性同步 context。若帧在 submit前失败，资源保持 Pending并在下一帧重试。启动必需 placeholder/font使用显式 bootstrap submission与初始化 completion。

## 11. StaticMesh、Material 与 Mesh 绘制链

### 11.1 StaticMesh

第一版 `StaticMesh` 支持：

- `StaticMeshSection`；
- Material Slot；
- Component material override；
- position、normal、UV0 vertex layout；
- UInt16或按资源明确声明的 index format。

Demo Cube实际只有一个 Section。第一版不支持 LOD、skeletal mesh、streaming与 runtime mesh mutation。

### 11.2 Game Thread Material

正式命名为：

```text
Material → MaterialInstance → MaterialRenderProxy
```

不使用 `MaterialTemplate`，不提前引入 `MaterialInterface`。

Game侧引用：

```text
MaterialRef         = shared_ptr<const Material>
MaterialInstanceRef = shared_ptr<MaterialInstance>
```

`StaticMeshComponent` 始终持有 `MaterialInstanceRef`。`Material` 是不可变资产定义；`MaterialInstance` 保存可编辑 override。参数通过稳定 `ShaderParameterId`和 typed value访问，不在通用 Material类中硬编码 Phong成员。跨线程时才提取 Render Resource ID与已 resolve参数快照。

### 11.3 第一版 Phong Material

Material静态属性：

- `shading_model = Phong`；
- `blend_mode = Opaque | Translucent`；
- `two_sided`；
- ShaderMap reference。

MaterialInstance参数：

- `base_color_factor : float4`；
- optional `base_color_texture`；
- `specular_color : float3`；
- `shininess : float`，必须不小于 1；
- `opacity : float`。

shader行为：

```text
surface_color = base_color_factor * sample(base_color_texture)
final_alpha   = base_color_factor.a * texture.a * opacity
```

缺少 base-color texture时绑定 white texture，不使用 shader分支。Opaque强制 Alpha 1。blend mode与 two-sided参与 PSO选择，不能由 MaterialInstance override。第一版不支持 normal map、emissive、alpha mask、PBR或 runtime shader graph。

### 11.4 MeshBatch 与 MeshDrawPacket

```text
PrimitiveSceneProxy
    → MeshBatch
    → ForwardMeshProcessor
    → MeshDrawPacket
```

`MeshBatch` 描述场景/材质语义，不保存最终 PSO或 BindingSet：

```cpp
struct MeshBatch
{
    PrimitiveId primitive_id;
    RenderStaticMeshSectionRef section;
    MaterialRenderProxyRef material;
    ObjectRenderDataRef object_data;
};
```

`ForwardMeshProcessor`结合 Pass状态生成可直接录制的 packet：

```cpp
struct MeshDrawPacket
{
    RHIGraphicsPipelineRef pipeline;
    std::vector<RHIVertexBufferBinding> vertex_buffers;
    RHIIndexBufferBinding index_buffer;
    RHIGraphicsBindings bindings;
    RHIDrawIndexedArgs draw_args;
    MeshDrawSortKey sort_key;
    PrimitiveId primitive_id;
};
```

这些字段直接复用现有公共 RHI descriptor，不建立第二套底层命令模型。

排序规则：

- Opaque优先 Pipeline、Material，再按 view-space depth近到远，最后按 PrimitiveId确保确定性；
- Translucent首先按 view-space depth远到近，Pipeline/Material只作同深度 tie-breaker；
- 两类 packet分列表，但在同一 Forward attachment scope中依次录制；
- 第一版不做 instancing、merging或 state bucket。

## 12. Visibility 与灯光

### 12.1 Visibility

Game Thread只计算 world transform与 world bounds。Render Thread对每个 SceneView执行线性 Frustum Culling。第一版无 octree、BVH、occlusion query或 HZB。

### 12.2 灯光预算与选择

Shader ABI固定：

```text
MaxForwardDirectionalLights = 1
MaxForwardLocalLights       = 8
```

运行配置 `ForwardMaxLocalLights` clamp到 `[0,8]`。Point与 Spot共享 local-light数组。每个 View独立选择：

1. 过滤 disabled、非法参数与 zero-intensity Light；
2. Directional按 `render_priority`降序、LightId升序取一个；
3. Point range sphere与 View frustum保守相交；
4. Spot使用包围影响锥的 conservative bounds；
5. local lights按 priority降序、camera到 influence bounds距离升序、LightId升序；
6. 截取配置数量，并统计 dropped count。

第一版不做 per-object light list。超限诊断必须节流，不能每帧刷屏。

### 12.3 Phong 与衰减

所有颜色在线性空间计算，法线、光线与 view direction统一使用 world space。第一版 intensity为明确记录的无量纲标量，不冒充 lumen/lux。

Directional与 Spot Component本地 `+Z` 表示光传播方向。Directional surface-to-light方向取传播方向的相反方向。

Local attenuation：

```text
distance_attenuation =
    saturate(1 - (distance / range)^4)^2
    / max(distance^2, minimum_distance^2)

spot_attenuation =
    smoothstep(cos(outer_angle), cos(inner_angle),
               dot(light_forward, light_to_pixel))
```

`range > 0`，`0 <= inner_angle <= outer_angle < 90 degrees`。Directional无距离衰减。Forward Pass提供一个 `ambient_color`，第一版不实现 environment lighting。

## 13. Binding Group contract

五个逻辑组职责固定为：

- `Global`：RenderFrame共享的 frame index、game time与 delta time；
- `View`：view/projection/view-projection及必要 inverse、camera position、ViewRect、target extent、near/far；
- `Pass`：Directional与 local light数组、count及 Forward所需 scene resources；
- `Material`：resolved参数、texture与 sampler；
- `Object`：local-to-world、normal matrix与可选 PrimitiveId诊断值。

静态 blend mode、two-sided等不作为普通 uniform上传，而是参与 MeshProcessor与 PSO选择。一个 BindingSet可被多个 DrawPacket共享。GPU布局必须由 Shader reflection验证，不依赖 C++ padding猜测。

五个逻辑组不等于 descriptor set。`VulkanPortable v1`继续使用：set 0=Global+View、set 1=Pass、set 2=Material、set 3=Object。D3D11/D3D12按 target/stage/register-class映射，上层不比较 native slot数字。

## 14. Renderer 生命周期与 Prepare

持久对象：

- `RenderScene`；
- `RenderViewport` / `RHIViewportContext`；
- `RenderResourceCache`；
- viewport与 SceneOutput render resources。

每个 `SceneViewFamily`临时创建一个 `ForwardSceneRenderer`，类似 UE 的每帧 `FSceneRenderer`。它持有 visibility、selected lights、MeshBatch/DrawPacket、prepared pass inputs与录制结果。未来工厂可创建 `DeferredSceneRenderer`；在未实现时请求 Deferred必须返回 `Unsupported`。

`PreparedRenderFrame`是 Apply结束后的只读帧快照。它强持有所有 recording所需资源，worker不得回读可变 RenderScene。Render Thread必须等同帧所有 worker结束后才处理下一 packet；第一阶段无 worker，仍遵守此 contract。

## 15. 显式 Pass 与资源

### 15.1 录制接口

Pass采用 UE风格但保持 Toy3d命名：

```cpp
render_*_pass(RHIGraphicsCommandContext& context)
```

Pass自己管理 `begin_render_pass/end_render_pass`。串行或并行只是 SceneRenderer私有策略：

```text
record_serial(...)
record_parallel(...)
```

Pass不知道得到的是共享 immediate context还是 worker独立 context。第一阶段只实现 `record_serial()`，不建立假的 async路径。

### 15.2 Scene targets

第一版固定：

```text
SceneColor = RHIFormat::R16G16B16A16Float，linear HDR，Alpha写1.0且无长期语义
SceneDepth = RHIFormat::D24UnormS8，DepthStencil | ShaderResource
```

Depth使用 0..1 reversed-Z：clear 0.0，compare `GreaterEqual`。Stencil第一版不用，clear 0。Vulkan创建前验证 D24 attachment与 sampling支持；D3D11未来使用 typeless resource分别创建 DSV与 depth-only SRV；D3D12同理由 backend管理兼容 resource/view格式。

`D24UnormS8`是第一版实际选择，不是永久移动 profile基线。后续移动端可 capability-driven选择 D32Float、D24UnormS8或 D16Unorm，而不改变 reversed-Z语义。

### 15.3 Forward Pass

Forward Pass在一个 SceneColor/SceneDepth attachment scope内完成：

1. clear SceneColor与 SceneDepth；
2. draw Opaque，depth test/write开启；
3. draw Translucent，depth test开启、depth write关闭、source-alpha blend；
4. end render pass；
5. transition SceneColor供 composition采样。

Opaque与 Translucent不得为了逻辑分类而中断 attachment scope，以避免移动 tile renderer发生不可控 load/store。第一版半透明不支持 refraction、SceneColor sampling、OIT、半透明阴影。

### 15.4 PostProcess 与颜色空间

Forward在线性 HDR SceneColor中完成光照。第一版 PostProcess为 fullscreen triangle：

```text
HDR SceneColor
    → manual exposure（SceneViewFamily参数，默认1.0）
    → ACES fitted tone curve
    → linear SDR [0,1]
```

不实现 auto exposure、Bloom、TAA、FXAA、vignette或 color grading。

最终 sRGB transfer只能发生一次。优先使用 sRGB attachment由硬件编码；surface不支持时，使用 linear LDR中间目标加显式 encode fallback，禁止在 sRGB数值空间直接混合 UI。ImGui位于 Tone Mapping后，不受 exposure影响。普通 UI texture/font按 sRGB资源采样并解码到线性；linear Offscreen SceneOutput按 linear texture处理。

### 15.5 Viewport显式顺序

```text
ResourceUpload
    ↓
每个 Offscreen Family：
    ForwardPass → FinalComposition(ToneMap → Offscreen)
    ↓ transition to ShaderResource
可选 Present Family：
    ForwardPass → FinalComposition(ToneMap → Backbuffer)
    ↓
ImGui draws
    ↓
Present
```

`FinalCompositionPass`是 viewport/output级 Pass，并管理最终 color attachment scope。有 Scene时先 tone-map fullscreen draw再绘制 ImGui；UI-only viewport clear背景后直接绘制 ImGui。UI不属于 Forward或未来 Deferred SceneRenderer。

在 RDG引入前，Pass与 orchestrator明确记录 transition。不得建立另一个通用资源声明/调度 API。后续 RDG接管 logical/transient resource、依赖编译、barrier、culling与调度，但保留 SceneRenderer、业务 Pass和 `render_*_pass(context)`的清晰职责。

## 16. ImGui 共同集成

### 16.1 线程归属

以下操作只在 Game Thread：

- ImGui Context；
- platform input与 `NewFrame()`；
- Widget构建；
- `ImGui::Render()`；
- 将结果深拷贝为 immutable `ImGuiDrawPacket`。

DrawPacket与 World/View数据进入同一个 `RenderFramePacket`，接受相同 one-frame lag。Render Thread不得访问 ImGui Context或 ImDrawList原始内存。

### 16.2 三层结构

```text
ImGui Core（第三方，不含官方 backend）
        ↓
ImGuiPlatformBridge（Toy3d，Game Thread）
        ↓ immutable ImGuiDrawPacket
ImGuiRHIRenderer（Toy3d，Render Thread，公共 RHI）
```

新建第一方 `Toy3dImGuiCore`构建目标，只编译 ImGui core源文件，不使用 `imgui_impl_vulkan`、`imgui_impl_glfw`或 `imgui_impl_win32`。现有第三方目录源码不修改；runtime以 PRIVATE方式依赖新 core target，避免 Vulkan/GLFW依赖向上传播。

PlatformBridge消费 Toy3d的 immutable per-frame platform event snapshot。事件 contract需补齐 key up/down、mouse、wheel、UTF-32 text character与 focus。同一事件快照同时供 Gameplay Input与 ImGui读取；`WantCaptureMouse/Keyboard`只影响上层路由，不在平台层丢事件。

第一版实现 clipboard与系统 cursor，关闭 ImGui Multi-Viewport，只启用 Docking。原生 IME composition与 Gamepad Navigation后置。

### 16.3 DrawPacket 与 Texture ID

DrawPacket深拷贝 vertex/index buffers、draw commands、clip rect、texture ID、vertex/index offset、display pos/size与 framebuffer scale。第一版 callback只支持 `ResetRenderState`；其他 callback诊断并跳过。

`ImTextureID`封装稳定 `ImGuiTextureId`，禁止保存 Vulkan handle、RHI指针或 descriptor。Render Thread registry将 ID resolve为 texture view、sampler、color-space metadata与具体资源版本。invalid ID使用可诊断 placeholder。

`ImGuiRHIRenderer`参考官方 Vulkan sample/backend的动态 vertex/index buffer扩容、scissor、projection与 offset行为，但所有创建、binding与 draw均通过公共 RHI。

## 17. 配置

沿用现有 `ConsoleManager`加载 INI与命令行，不在本批新增 UE式 CVar系统。Composition Root读取并验证字符串配置，生成类型化快照：

```cpp
struct RendererConfig
{
    bool multithreaded = true;
    bool one_frame_thread_lag = true;
    bool frustum_culling = true;
    std::uint32_t max_forward_local_lights = 8;
};
```

INI：

```ini
[Renderer]
Multithreaded=true
OneFrameThreadLag=true
FrustumCulling=true
ForwardMaxLocalLights=8
```

Renderer、RenderScene与 Pass不得直接轮询全局 `ConsoleManager`。第一版这些配置初始化后不可变；单线程强制关闭 lag。未来 Editor动态设置通过带 revision的 `RendererSettingsUpdate`在 packet帧边界生效。

## 18. 错误模型

错误分三类：

```text
ContentError      → placeholder或跳过单个对象
RecoverableFrame  → 放弃本帧并在后续帧恢复
FatalRenderer     → 停止接收 packet并有序关闭
```

每个 packet必须且只能完成一次 `RenderFrameCompletion`。completion RAII guard兜底所有 early return。错误通过显式 status传回 Game Thread，不跨线程抛异常。

处理策略：

- viewport `NotReady`/最小化：本帧不 render/present，视为可恢复完成；
- `OutOfDate`：放弃该 viewport帧并安排 Render Thread重建；其他 viewport可继续；
- Pass必需资源或录制失败：`abort_frame()`并丢弃未提交 command list；
- Present失败：报告已提交 GPU工作的结果，按状态重建或终止；
- `DeviceLost`、Render Thread未捕获异常或关键同步失效：进入 terminal state，拒绝新 packet并唤醒全部等待者；
- 正常失败不得直接 `wait_idle()`，只有明确的 viewport重建、flush或 shutdown可执行所需同步。

未来并行录制中，任一 worker失败后仍须等待同帧其他 worker退出，再丢弃所有录制结果并完成 completion。

## 19. Pass 间并行与后续 RDG

第一阶段只有 Game/Render双线程，所有 Pass在 Render Thread串行录制。不使用 `std::async`，不在 renderscene内封装临时线程池。

第二阶段在显式 Forward Renderer稳定后：

1. 先为共享 `TaskSystem`形成独立 `engine/core/`设计；
2. 为 Vulkan/D3D12实现独立 graphics recording context capability；
3. Prepared输入保持 immutable；
4. 首个并行用例为未来 Shadow Pass与 Forward Pass的 Pass间并行；
5. 按依赖顺序提交完整 Pass command lists；
6. D3D11 capability为 false并安全退化到相同 serial路径。

不做 UE式单 Pass draw-range拆分。Forward内部 Opaque/Translucent始终保持一个 attachment scope。

RDG在完整渲染器、显式资源生命周期、至少 Shadow/Forward依赖和 Pass并行需求得到实际验证后引入。RDG最终接管资源声明、依赖图、barrier、transient生命周期、pass culling与调度；它不进入公共 RHI，也不改变 Game/Render scene镜像、Material、MeshProcessor或 ImGui线程边界。

## 20. Backend 可实现性

### 20.1 Vulkan

- 第一里程碑实际运行 backend；
- D24S8 depth sampling使用 depth aspect view；
- Global+View按现有 binding aggregation映射到 physical set 0；
- negative viewport height与 native front-face修正由 backend承担；
- upload、transition、render pass与 present沿用 frame-local context和 local state tracker。

### 20.2 D3D11

- 基线 FL11_0/SM5；
- Device/Render Thread使用 immediate context串行录制；
- D24S8使用 typeless resource、DSV与 depth-only SRV；
- logical Binding Group按 stage/register class映射；
- 无 Pass并行 capability时走 `record_serial()`；
- resource transition映射为 hazard跟踪与冲突 binding解除，不伪造 D3D12 barrier。

### 20.3 D3D12

- command list、resource state与 descriptor heap由 backend管理；
- binding按 target mapping/root signature策略实现；
- 后续可支持独立 Pass recording contexts；
- SceneRenderer不依赖 root parameter或 heap handle。

### 20.4 VulkanPortable / Mobile

- 保持 Vulkan 1.1、SPIR-V 1.3与最多四个 bound descriptor sets；
- Forward Opaque/Translucent不打断 attachment scope；
- D24S8须查询 format support，不能提升为移动必需格式；
- 不在 Shader中手写 Y flip或平台分支；
- Cook与 runtime继续验证 profile、limits与 required capabilities。

“已评估可实现”不表示对应 backend已经实现或通过运行验收。

## 21. 第一里程碑 Demo 与验收

### 21.1 Demo Scene

```text
Camera
├── 3 × 3 Opaque Cube，各自不同旋转速度
├── 1 Translucent Cube
├── 1 Directional Light
├── 2 Point Light
└── 1 Spot Light
```

ImGui显示 FPS、Game/Render frame ID、可见 Primitive数、selected/dropped Light数与当前线程模式。另用十个 local lights的自动/诊断场景验证上限截断，默认视觉 Demo不堆叠十盏灯。

### 21.2 线程与生命周期验收

必须覆盖：

```text
Multithreaded=true,  OneFrameThreadLag=true
Multithreaded=false, OneFrameThreadLag=false
```

两条路径都必须完成 resize、最小化/恢复、正常退出、失败 completion与资源安全销毁。

### 21.3 自动测试矩阵

- 强类型 ID invalid、非复用与类型隔离；
- Transform hierarchy、cycle、KeepWorld与非法 scale；
- dirty合并优先级；
- RenderScene Add/Update/Remove协议；
- Frustum Culling；
- 灯光选择、稳定排序与 `0..8`截断；
- Opaque/Translucent packet排序；
- Material revision、placeholder与旧版本生命周期；
- queue容量、lag、flush、shutdown与 completion必达；
- ImGui DrawPacket深拷贝、clip、offset与 invalid TextureId；
- RHI `RGBA16F render→sample`；
- RHI `D24S8 depth attachment→depth SRV`；
- RHI upload-before-draw与 alpha blend。

实际 Vulkan运行验收须无 validation error。若当前 RHI缺少 readback，视觉结果先以可复现的人工截图/RenderDoc检查作为补充证据，不得把只有单元测试描述为完整画面验收。

## 22. 分批执行计划

### 批次 1：文档与路线收敛

- 完成本文件；
- 同步 RHI requirement/current review、RHI design与 Shader design中的 RDG路线；
- 不修改代码；
- 检查术语、链接、路线与未决项一致性。

完成条件：不再存在“RHI后立即 RDG”或“Material必须通过 RDG接入”的冲突表述。

### 批次 2：RHI最小缺口

- 验证/补齐 RGBA16F render→sample；
- 验证/补齐 D24S8 DSV/depth-only SRV；
- 补齐 Renderer所需的 format capability与 recoverable viewport status；
- 核对 `abort_frame()`是否满足完整失败路径；
- 增加独立 RHI tests。

完成条件：不依赖 RenderScene即可证明目标 format、usage、transition与失败帧语义成立。

### 批次 3：GameScene领域模型

- World、Actor、SceneComponent hierarchy；
- Mesh、Camera与三类 Light Component；
- Material/MaterialInstance引用；
- 强类型 Render ID、dirty与 batch合并；
- 完成纯 CPU测试。

### 批次 4：Render Frame Transport

- packet、bounded queue与 completion；
- Render Thread lifecycle；
- one-frame lag与 single-thread fallback；
- 用 fake `RenderFrameProcessor` 覆盖成功、失败、flush与 shutdown。

### 批次 5：RenderScene与资源镜像

- Primitive/Light Info与 Proxy；
- 增量 Apply协议；
- RenderResourceCache、revision、placeholder与 upload；
- 不要求完整画面，先验证镜像与生命周期。

### 批次 6：SceneView与 Forward Prepare

- ViewFamily/View与矩阵；
- Frustum Culling与灯光选择；
- MeshBatch、ForwardMeshProcessor与 MeshDrawPacket；
- Opaque/Translucent排序；
- Prepared Frame不可变边界。

### 批次 7：Forward与 FinalComposition

- Phong Shader/Material binding；
- Forward attachment scope；
- ACES Tone Mapping与颜色空间；
- 得到无 ImGui的 Cube画面。

### 批次 8：ImGui共同集成

- ImGui Core target与 PlatformBridge；
- immutable DrawPacket与 TextureRegistry；
- ImGuiRHIRenderer接入 FinalComposition；
- scene overlay与 UI-only路径。

### 批次 9：Demo、迁移与总验收

- 完成 3×3 Cube、透明 Cube与三类灯光；
- 验证两种线程模式、resize/minimize/shutdown；
- 完成自动测试与 Vulkan运行证据；
- 删除旧 `SceneRendering/test_pass`及其唯一入口；
- 更新文档中的实际完成状态。

实现代码完成后，必须由独立 sub-agent执行构建与测试验证；主 agent根据结果修复并最终复查。

## 23. 旧实现删除条件

现有 `engine/runtime/renderscene/3dscene/scene_render.*`与 `pass/test_pass.cpp`属于 RHI bring-up路径，不是长期 Renderer入口。它们在以下条件全部满足后删除：

1. 新 Render Thread与 single-thread fallback均可运行；
2. 新 Forward Renderer完成 Cube draw；
3. ShaderMap、五组 Binding、RGBA16F、D24S8与 Present均走新路径；
4. resize、abort、shutdown测试通过；
5. ImGui已由公共 RHI路径绘制；
6. 新 Demo成为 `Toy3dEditor`唯一正式渲染入口。

迁移期间允许旧 test pass作为短期验证基线，但不得新增功能，不得形成两个长期正式入口。删除时同时删除其专用 shader、资源初始化与 CMake登记。

## 24. 明确后置路线

第一里程碑之后建议顺序：

1. Directional single shadow map；
2. `engine/core/` TaskSystem独立设计与实现；
3. Shadow/Forward Pass间并行录制，D3D11串行退化；
4. Editor Offscreen Scene View与 PlayWorld；
5. D3D11 backend用于跨 API验证；
6. Deferred Renderer factory实现；
7. 更完整阴影、移动 profile与性能预算；
8. 在真实依赖与资源生命周期经验基础上设计 RDG；
9. RDG逐步接管 barrier、transient资源、culling与调度；
10. compute与 async compute独立后置。

该顺序不是要求在 RDG前实现所有渲染特性，而是要求先让 RDG面对真实 Renderer问题，而不是用图框架代替尚未建立的场景、材质、线程和 Pass语义。
