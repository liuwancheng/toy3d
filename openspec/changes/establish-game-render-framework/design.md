## Context

见 `proposal.md`。本文中的 `Engine` 始终特指 `engine/runtime/engine.h` 声明、`engine/runtime/engine.cpp` 实现的现有 `toy3d::Engine`，不表示新模块或新抽象层。当前该类已经事实拥有 Platform、Window、RHIDevice、viewport、旧 RenderResourceCache 和具体 SceneRendering，并在 Game Thread 直接 begin/render/end frame；仓库同时存在 Task Graph/RenderCommand 原型与旧 frame dispatcher。新设计保留底层 Task Graph named queue、RHI local state、queue completion、deferred deletion、viewport abort 和 bootstrap context，删除上层双轨并把 RHI/RenderScene 可变状态迁入 logical Rendering Thread。

本文件是多次 apply 的主导航文档。行为细节以 `specs/game-render-framework/**/spec.md` 为准；任何新增第一方具名类型都必须先确认 UE4.27 对应术语、Toy3d 实际职责和最终名称，再登记到唯一所属子 Spec 的 `Type Contracts`。未经确认的候选名不得进入正式 artifact 或实现。

## Game / Render 模块分侧

```text
Game side（Game Thread 可变）
├─ engine/runtime/engine.h::toy3d::Engine
│   ├─ Platform / Window / RHISurface 生命周期
│   ├─ TaskGraph start/shutdown
│   ├─ RenderingThread controller + Renderer shell ownership
│   └─ Game loop / FrameEndSync
├─ GameScene
│   └─ World / Actor / Component
├─ Asset 与 MaterialInstance 的 GT state
└─ SceneView / SceneViewFamily 的一次性值构造

Bridge contracts（不拥有业务可变状态）
├─ RenderCore RenderCommand / RenderCommandFence
├─ SceneInterface
└─ RenderResource lifecycle façade

Render side（logical Rendering Thread 可变）
├─ RenderingThread / Renderer
├─ RenderScene / PrimitiveSceneInfo / PrimitiveSceneProxy
├─ SceneRenderer / ViewInfo / visibility
├─ MeshBatch 收集与 ForwardSceneRenderer Base Pass
├─ VertexFactory / LocalVertexFactory
├─ RenderResourceManager / render representations
└─ public RHI / backend execution
```

`Plane` 与 `ConvexVolume` 是共享数学值类型，不依赖 Renderer 或 RHI。`SceneView`、`SceneViewFamily` 是 GT 构造的一次性值；`ViewInfo` 是 SceneRenderer 在 RT 使用的 per-view 状态；`MeshBatch` 是 frame-local、non-owning 的 mesh draw 输入。

Game side 不包含 RenderScene、RHI 或 backend 类型；Render side 不读取 World、Actor、Component、Camera 等 Game 对象。Bridge 可以声明 command、同步、stable identity 和 ownership-transfer contract，但不能积累一套独立业务状态。代码目录继续使用当前职责明确的 `gamescene`、`rendercore`、`renderscene` 和 `drivers/rhi`，不新建笼统的 Engine 模块。

## 一帧主流程

```text
Game Thread                         Rendering Thread                         GPU
────────────────────────────────────────────────────────────────────────────────────

Frame N
  │
  ├─ World tick
  ├─ Component/Asset/Material GT state updates
  │
  ├─ enqueue Scene add/update/remove ────────→ update RenderScene
  ├─ enqueue Material setters ──────────────→ update MaterialRenderProxy + dirty
  ├─ enqueue Resource init/update ──────────→ register PendingUpload
  │
  ├─ build SceneView values
  ├─ build SceneViewFamily
  ├─ create one-shot SceneRenderer
  │
  ├─ enqueue Draw(move SceneRenderer) ──────→ DrawScene
  │                                            │
  │                                            ├─ begin_frame
  │                                            │    └─ NotReady/OutOfDate:
  │                                            │       no business recording;
  │                                            │       pending resources remain
  │                                            │
  │                                            ├─ create one graphics context
  │                                            ├─ record_pending_uploads
  │                                            ├─ init_views
  │                                            │    ├─ build ViewInfo matrices
  │                                            │    └─ build Plane / ConvexVolume
  │                                            ├─ compute_view_visibility
  │                                            │    └─ cull PrimitiveSceneInfo bounds
  │                                            ├─ collect MeshBatch from visible proxies
  │                                            ├─ match ShaderVertexInput with LocalVertexFactory
  │                                            ├─ resolve View/Material/Object bindings
  │                                            ├─ ForwardSceneRenderer::render_base_pass
  │                                            ├─ finish immutable command list
  │                                            └─ end_frame(frame, {list})
  │                                                   │
  │                              business submit failed│business submit succeeded
  │                                      ┌─────────────┴──────────────┐
  │                                      ▼                            ▼
  │                              discard resource             commit resource Ready
  │                              recording transaction        commit RHI final state
  │                              keep initial payload          retain in-flight payload
  │                              recover or terminal           process present status
  │                                                                   │
  │                                                                   └──────────────→ execute GPU work
  │                                                                                       │
  ├─ enqueue RenderCommandFence N ──────────→ RT CPU reaches Fence N                     │
  └─ FrameEndSync waits Fence N-1                                                       completion
                                                                                          │
                                                                                          └─ recycle staging/list
                                                                                             deferred native deletion
```

三个完成语义必须始终分开：

```text
RenderCommandFence      = Rendering Thread CPU 到达
business submit commit  = RenderResource Ready + RHI committed access
queue completion        = GPU 已完成，可回收 in-flight payload
```

submit 成功但 present 为 `Suboptimal`、`OutOfDate` 或 terminal 时，business work 已经发生，因此必须先 commit resource/RHI state，再处理 presentation 状态，绝不能回滚。

`init_views()` 失败时不得录制业务 pass，已 acquire 的 frame 必须通过 `abort_frame()` 闭合。VertexFactory 与 Shader vertex input 不兼容时不得创建残缺 pipeline；对应 `MeshBatch` 必须被跳过并保留可诊断错误。viewport `NotReady`/`OutOfDate` 不消费 pending uploads。

## 多次 Apply 导航

| 阶段 | Capability | 直接前置 | 完成信号 |
| --- | --- | --- | --- |
| 0 | `legacy-rendering-cleanup` | 无 | 废弃清单无正式符号/CMake/test 残留，底层可复用测试仍通过 |
| 1 | `task-graph-runtime` | 0 | 单 active、attach、shutdown 测试通过 |
| 2 | `rendering-thread-lifecycle` | 1 | multi/single start/pump/join 测试通过 |
| 3 | `render-command-transport` | 2 | move-only、FIFO、inline、worker rejection 通过 |
| 4 | `frame-synchronization` | 3 | Fence、one-frame lag、flush、terminal wake 通过 |
| 5 | `renderer-scene-ownership` | 3 | Renderer/World/RenderScene 创建与 teardown 通过 |
| 6 | `engine-composition-root` | 1、2、4、5 | Engine 显式拥有框架对象且不再直接执行 frame/RHI |
| 7 | `primitive-proxy-lifecycle` | 5、6 | add/update/remove/ownership/ordering 通过 |
| 8 | `view-render-flow` | 4、7 | multi/single 一帧 SceneRenderer 闭环通过 |
| 9 | `rhi-frame-submission` | 8 | submit/present/abort/resize 结果矩阵通过 |
| 10 | `rhi-resource-state` | 9 | record/submit/discard/completion/deletion 状态矩阵通过 |
| 11 | `render-resource-manager` | 9、10 | pending/commit/discard/release/terminal 通过 |
| 12 | `static-mesh-resources` | 11 | buffer init、ready gate、failure/replacement 通过 |
| 13 | `texture-resources` | 11 | content update、candidate、binding generation 通过 |
| 14 | `material-updates` | 7、13 | setter FIFO、lazy materialize、reference/replacement 通过 |
| 15 | `renderer-bootstrap` | 1、3、9、11 | placeholder 全有或全无、viewport 与失败回滚通过 |
| 16 | `renderer-terminal-shutdown` | 5、6、11、15 | normal drain、terminal skip、DeviceLost teardown 通过 |
| 17 | 总集成 | 全部 | Editor 多/单线程、resize/minimize、启动失败、正常退出和 terminal 全通过 |

Capability 依赖仍决定正式入口的发布顺序，但施工和验证按以下较大批次推进，不再要求每个框架空壳单独建立测试 target：

```text
Batch A（已完成基础设施）
Task Graph / RenderingThread / RenderCommand /
FrameSynchronization / Renderer shell

Batch B（Game / Scene / View 框架）
Engine composition root
SceneInterface lifecycle
PrimitiveSceneProxy / PrimitiveSceneInfo
SceneView / SceneViewFamily / ViewInfo
SceneRenderer / visibility skeleton

Batch C（真实 Forward 纵向闭环）
RHI vertex-input reflection
RenderResourceManager
StaticMesh / Texture / Material representations
VertexFactory / LocalVertexFactory
MeshBatch / Forward Base Pass
submit / present

Batch D（集中测试与收尾）
CPU visibility
single/multi-thread end-to-end
resource update/remove
resize/minimize/restore
failure matrix / real Vulkan smoke
terminal / shutdown
```

Batch B、C 的中间门槛是 CMake configure、受影响正式 target 编译和少量生命周期或真实 draw smoke。完整 visibility、端到端与 failure matrix 集中到 Batch D。若实现发现新类型，先取得名称确认、修改对应 Spec、运行 strict validation，再继续代码。

## Goals / Non-Goals

**Goals:**

- 现有 `engine/runtime/engine.h::toy3d::Engine` 成为显式 composition root，但不成为 service locator，也不衍生第二层 Engine 抽象。
- 除该具体 Engine 类外，其余模块明确归属 Game side、Render side 或无状态 bridge。
- Task Graph named RenderingThread queue 成为唯一 Game-to-Render transport。
- Renderer-owned RenderScene 与 Asset-owned render representation 具有清晰线程/所有权边界。
- 资源录制事务、业务 submit 和 GPU completion 的失败语义闭合。
- 规划可按 capability 多次 apply，每阶段有独立测试和删除门槛。

**Non-Goals:**

- RHI Thread、软件 replay、AnyWorker RenderCommand producer、pass 内并行。
- streaming、upload byte budget、async compute、多 graphics queue、RDG、bindless。
- 完整 headless Renderer；保留 device bootstrap 和独立 RHI 测试路径。
- screenshot/readback 等带业务返回值的异步协议。

## Decisions

### 1. 先清旧路径，不建设兼容层

旧 `RenderFramePacket/Dispatcher/Queue/Completion`、旧 Scene frame processor、RenderResourceCache/collector、render-resource ID/revision 和空壳 `RHIDeviceCommandList` 会引导调用方继续依赖错误边界。阶段 0 先形成符号/CMake/test 删除清单；底层 Task Graph 与 RHI 正确基础保留。替代方案“新旧并行，最后切换”被否决，因为它会要求 Engine 同时编排两套线程和资源生命周期，无法验证 ownership。

阶段 0 允许为了保持 build green 暂时删除尚无新替代的上层 demo 路径；后续 capability 按依赖逐步恢复正式功能。不得用 no-op adapter 假装旧接口仍成功。

### 2. 现有 toy3d::Engine 是 GT composition root，Renderer 是 RT domain

```text
toy3d::Engine (engine/runtime/engine.h, GT-owned)
├─ Platform / Window / RHISurface
├─ unique TaskGraphInterface
├─ RenderingThread lifecycle controller
├─ stable Renderer shell
└─ FrameEndSync + Game loop

Renderer (logical RT mutable domain)
├─ RHIDevice / graphics queue
├─ primary RHIViewportContext
├─ RenderScene
├─ RenderResourceManager
├─ placeholder/cache refs
└─ first terminal status
```

现有 `toy3d::Engine` 创建 Renderer 稳定外壳，让 RenderingThread 在 logical RT 初始化内部状态；GT 只能通过专用 façade 和 FrameEndSync/Fence 结果交互。该类不提供全局 getter，Renderer 也不暴露 manager/device getter 给业务模块。不得为这些职责新增 `EngineDomain`、`EngineContext`、`EngineServices`、`RuntimeEngine`、`GameEngine` 或等价 wrapper。

选择原因是 Task Graph、OS Rendering Thread 和 Renderer 的创建/关闭必须在进程入口统一排序，但 Scene/RHI 可变状态必须留在 RT。替代方案“Task Graph 自己创建 RenderingThread/Renderer”会造成基础设施反向依赖业务；“Renderer 创建 Task Graph”则把进程调度生命周期塞入渲染域，均被否决。

### 3. Task Graph 是显式拥有的单 active instance

Engine 使用 `unique_ptr` 显式创建和 shutdown Task Graph；`TaskGraphInterface::get()` / `is_running()` 只访问 active instance，不拥有它、不隐式初始化。不是 Meyers singleton。当前产品不支持并存多个 Task Graph，因此不为假设中的多实例增加 route/binding。

### 4. RenderCommand 无 RHI 参数且不做服务查找

普通 callable 固定 `void() noexcept`，具体类型直接存在 GraphTask 中。GT+multi enqueue、RT inline、GT+single inline、Worker 禁止。命令捕获 stable target 与 owned payload，调用 Scene/resource 的窄 API；不传 `RHIDeviceCommandList&`，不查找 Renderer/manager。

### 5. Scene bridge 使用 ownership transfer 与稳定地址

```text
Renderer owns RenderScene
World holds non-owning SceneInterface*
Component holds opaque non-owning PrimitiveSceneProxy*
RenderScene/PrimitiveSceneInfo owns PrimitiveSceneProxy
Draw command owns one-shot SceneRenderer
```

Proxy 在 GT 创建后 move 到 Add command；transform update 捕获 pointer identity、copied `Matrix4`、copied `AxisAlignedBounds` 与 `bool visible`，不新增具名 payload；remove 先摘除索引后 RT 析构。稳定对象地址是同步 FIFO identity，不建立 Proxy ID/revision。GT 禁止解引用 opaque pointer。第一阶段 StaticMesh 或 material-slot identity 变化通过 destroy 后 create 重建 Proxy；MaterialInstance 内部参数值变化仍走稳定 MaterialRenderProxy 的专用更新路径。

`SceneView`、`SceneViewFamily`、`SceneRenderer` 是一次性输入，禁止以 `Snapshot` 命名。GT 构造时不得读取 RT RenderScene；SceneRenderer move 进 Draw 后由 RT 查询 FIFO 更新后的场景并析构。

Component 与 SceneInterface 使用 UE 容易识别的生命周期方向：

```text
PrimitiveComponent::create_render_state()
→ SceneInterface::add_primitive()
→ RenderCommand transfers PrimitiveSceneProxy ownership
→ RenderScene creates PrimitiveSceneInfo

PrimitiveComponent::send_render_transform()
→ SceneInterface::update_primitive_transform(
    proxy, world_transform, world_bounds, visible)
→ RT updates proxy-owned transform, bounds and visibility

PrimitiveComponent::destroy_render_state()
→ SceneInterface::remove_primitive()
→ RT removes SceneInfo from visibility/draw collections
→ RT destroys proxy
```

已经实现的 `World::bind_scene()` / `unbind_scene()` 保留。World 已有注册 Primitive 后再绑定 Scene 时必须补建 render state；所有无返回值 Add 正常返回后才完成绑定。重复绑定是唯一正常返回 false 的 bind 分支，transport contract violation 不得转换为 partial rollback。解绑时必须先投递所有 Primitive remove，再清空 non-owning SceneInterface。

### 6. SceneView、ViewInfo 与 visibility 使用 reversed-Z 统一约定

`SceneView` 复制 camera position/orientation、projection mode、FOV、near/far 与 viewport rect，不保存 Camera、Window 或 RenderScene 引用。`ViewInfo` 在 logical RT 由 `init_views()` 构造 view、reversed-Z projection、view-projection、camera vectors、`ViewUniformShaderParameters` 与 `ConvexVolume`。

Toy3d 的 clip 条件固定为：

```text
-w <= x <= w
-w <= y <= w
 0 <= z <= w
```

设 view-projection 的数学行向量为 `r0/r1/r2/r3`，朝向视锥内部的平面为：

```text
Left   = r3 + r0
Right  = r3 - r0
Bottom = r3 + r1
Top    = r3 - r1
Far    = r2
Near   = r3 - r2
```

finite perspective 使用六面；infinite-far projection 不启用 far plane。AABB 接触平面视为可见。第一阶段 `compute_view_visibility()` 线性遍历 `PrimitiveSceneInfo`，不引入 octree、occlusion、distance culling 或 LOD。

### 7. RenderResourceManager 是 RT-only non-owning lifecycle manager

```text
Uninitialized → PendingUpload → Ready → Released
                         └────→ Failed
```

Manager 只维护 non-owning pending pointers 和本次 recording collection，不承担 Asset cache、registry、ownership、Scene/Material 规则或 backend allocation。`upload_*()` 返回前复制到 RHI staging，但 CPU initial payload 保留到 business submit 成功；abort/submit failure 仍可重录。

正常 release 先排队移除 Scene/Material 引用，再把 owning representation move 进最后一条 release command。RT 从 manager 移除、release RHI refs并析构；实际 native object 由 command list/in-flight refs保活到 completion，不 wait idle。

### 8. Mesh、Texture、Material 与 VertexFactory 分开建模

StaticMeshRenderData 不继承 RenderResource；各 vertex/index buffer 独立继承，VertexFactory 是独立 RT object。整体 ready gate 防止部分资源 Draw，replacement 用完整 candidate RenderData。

```text
StaticMeshRenderData
├─ PositionVertexBuffer
├─ StaticMeshVertexBuffer
├─ optional ColorVertexBuffer
├─ StaticMeshIndexBuffer
└─ LocalVertexFactory
```

第一阶段 `StaticMeshRenderData` 只表达当前 StaticMesh Asset 的单组 geometry/sections，不增加 LOD 子结构，也不实现 LOD selection、streaming 或 partial residency。固定支持 `POSITION0`、`NORMAL0`、`TEXCOORD0` 与 optional `COLOR0` streams。

全部必要 buffer upload/transition 与 LocalVertexFactory validation 成功后，candidate 才可在当前 command list 内按 upload-before-draw 顺序参与 `MeshBatch`；长期 Ready 仍只能在包含这些资源的 business submit 成功后发布。abort 或 submit failure 不发布 Ready，并保留 CPU initial payload 供后续有效 frame 重录。Material 决定 ShaderMap Program、参数和 render state；VertexFactory 不选择 Material 或 Shader permutation。

`Texture` 是 GT/Asset-side Texture2D asset，拥有 `TextureDesc`、CPU initial payload 和地址稳定的 `TextureResource` allocation；MaterialInstance 通过 `TextureRef` 覆盖 RT 使用期。`TextureResource` 继承 RenderResource，在 RT 保存 active/candidate RHI texture/view。内容更新不更换 view 或 binding generation；descriptor/format/mip replacement 只有在 submit 成功后发布并递增 RT-only binding generation。

MaterialRenderProxy 不继承 RenderResource。普通 setter 只更新参数表和 dirty；Draw 前按需生成 frame-local constants/binding。结构性 shader/layout/render-state 变化使用完整 candidate replacement。

### 9. VertexFactory 只桥接 Shader 输入与 geometry streams

```text
Shader compiler ReflectedInterfaceVariable
→ ShaderMap runtime ShaderVertexInput
→ LocalVertexFactory / VertexStreamComponent matching
→ RHIShaderDesc / RHIShaderVertexInputReflection
→ RHIGraphicsPipelineDesc vertex layout
```

`ShaderVertexAttributeId` 由规范化 semantic + index 生成。Shader 要求的每个普通 vertex input 必须恰好匹配一个 stream component；多余 mesh attribute 可以忽略，只把 Shader 实际使用的 attribute 写入 pipeline，并按 shader location 确定性排序。缺失输入或物理 format 与 scalar/component contract 不兼容时明确失败。

HLSL vertex entry signature 是唯一逻辑 schema；不新增 `.shader VertexLayout`。第一阶段不实现 UE 的 `VertexFactoryType` 全局注册、VertexFactory shader parameters、独立 VertexFactory permutation domain、manual vertex fetch 或 GPU Scene。Vulkan backend 使用 shader location；D3D11/D3D12 backend 使用 semantic name/index；RenderScene 与 VertexFactory 不做 backend 判断。

### 10. RHI frame-end 的权威是业务 submit

```cpp
struct RHIFrameEndResult
{
    RHIQueueCompletionValue completion_value = 0;
    RHIStatus presentation_status;
};
```

外层 `RHIResult` 成功表示 business lists 已 submit；失败表示未 submit。所有 validation 前移到 native submit 前。submit 后的 state/resource commit、last-use、in-flight retain 不返回可恢复失败；如果内部不变量破坏，仍报告“已提交 + completion”并 latch terminal。

abort 的最小 submit 只闭合 acquire synchronization，不算业务 submit。Resize 只要求等待该 viewport in-flight completion；Vulkan `vkDeviceWaitIdle()` 可暂作 backend 简化实现，不成为公共 contract。

### 11. local state、committed state 与 completion 分层

录制 transition 只修改 command-list local tracker；queue 按实际 submit 顺序发布 committed state，无需等待 GPU。discard/submit failure 不发布 final state。Completion 只回收 allocator/staging/descriptor/list/native resource。

公共 RHI object 必须具有不可变 device ownership identity，在 native 调用前拒绝跨 device 混用。Vulkan/D3D12 显式 barrier；D3D11 维护逻辑 hazard，GPU completion 使用 FL11_0 event query 或等价机制。

### 12. Renderer bootstrap 与 shutdown 分开处理

初始化：

```text
Engine Platform/Window/Surface
→ Task Graph active
→ RenderingThread attach
→ RT create RHIDevice
→ RT create RenderResourceManager
→ placeholder device submission + wait completion
→ primary viewport
→ publish Renderer Running
→ enable façades
```

正常 shutdown：

```text
GT stop producer
→ enqueue World/Proxy remove + resource release
→ internal final Renderer teardown task
→ RT abort active frame / clear Scene / clear manager
→ queue completion/deferred deletion / destroy viewport/placeholders/device
→ Fence/wait result
→ request return + join RenderingThread
→ shutdown Task Graph
→ destroy Renderer shell/Window
```

Terminal 路径先 latch first error、停止新 frame/init、discard recording、清 manager non-owning pointers，再 skip pending callable并在 RT 析构 payload。DeviceLost 不无限 wait/retry，secondary error 不覆盖 first error。

### 13. Type Contracts 与最小实现示例是 apply gate

每个具名类型只有一个 owning Spec。Type Contracts 必须说明：

- 类型名称及新增/现有修改状态；
- 稳定领域职责，禁止只为访问控制或模板形状制造 `Key/Token/Enabler/Storage`；
- CPU owner、non-owning observers 和 ownership transfer；
- mutable thread、create/init/release/destroy thread；
- 状态与错误语义；
- 不能复用现有类型的原因。

如果实现能使用局部变量、标准容器、直接值、函数或既有类型完成，就不得新增类型。需要新增时先修改子 Spec 并 strict validate；tasks 不授予绕过此门槛的权限。

每个新增或修改运行时代码的 capability spec 必须包含 `Minimal Implementation Example`。示例是 non-normative，只说明 CPU owner/non-owning observer/ownership transfer、GT/RT mutable thread、正常调用顺序和至少一个失败路径；它不得自行创造公共 API，也不得使用未经确认的类型名。若示例与 `Type Contracts` 或 requirements 冲突，以规范性内容为准。

## Risks / Trade-offs

- [阶段 0 删除旧 demo 路径会暂时减少可运行功能] → 每个下游 capability 尽快恢复纵向闭环，阶段门槛优先保持 build/test green，不保留 no-op adapter。
- [稳定裸指针依赖 FIFO 生命周期] → 只通过专用生命周期 API 排序，normal release 强制 ownership transfer，并覆盖悬空窗口测试。
- [单 active Task Graph 降低多实例灵活性] → 这是已确认 process model；保持显式 ownership/shutdown，未来改变产品模型时先改 Spec。
- [Type Contracts 增加规划维护成本] → 换取多次 apply 的命名稳定性，避免临时 wrapper 扩散；标准库和编译器生成类型豁免。
- [无 upload budget 造成首帧尖峰] → 第一阶段先闭合正确性并记录统计，真实 workload 后单独设计 streaming/budget。
- [单 command list 限制 CPU 录制并行] → 公共 end frame 保留 list vector和 context seam，状态接缝/RDG 未就绪前不提前并行。
- [submit 后错误不可回滚] → 前移 validation；提交后明确返回 completion并 terminal，优先保证生命周期真相。
- [Shader compiler 已持久化 interface variables，但 runtime loader 当前丢失] → 在 VertexFactory 接入前先闭合 ShaderMap metadata 转换与验证，禁止 runtime 猜测 vertex signature。
- [D3D input layout 依赖 semantic，而当前 RHI pipeline 主要表达 location] → 通过 `RHIShaderVertexInputReflection` 同时携带 semantic/index/location，由 backend 选择等价 native 映射。
- [第一阶段每帧线性 visibility 与 MeshBatch 收集成本较高] → 先验证真实纵向数据和所有权；空间索引、cached draw 与并行收集在有 workload 证据后独立设计。

## Migration Plan

1. 以已 strict validate 的本 change 和 `document/index.md` 作为唯一规划入口，从阶段 0 开始执行。
2. 按导航阶段 0 清理废弃路径，保留底层 Task Graph/RHI 基础。
3. 建立 Task Graph active instance、RenderingThread lifecycle、RenderCommand/Fence/FrameEndSync 与最小 Renderer shell。
4. 将这些已定型对象直接接入 `engine/runtime/engine.h/.cpp::toy3d::Engine`，移除该类对 frame/RHI 的直接执行，不新增第二层 Engine 抽象。
5. 完成 Batch B：建立 RenderScene/Proxy/SceneView/SceneViewFamily/ViewInfo/SceneRenderer 与 visibility 框架，中间只执行配置、正式 target 构建和少量 lifecycle smoke。
6. 闭合 ShaderMap 与 RHI vertex-input metadata，再实现 VertexFactory/LocalVertexFactory 和 MeshBatch 收集。
7. 完成 Batch C：建立 RenderResourceManager，迁移 Mesh、Texture、Material，并由 ForwardSceneRenderer Base Pass 恢复真实 Editor draw、submit 与 present。
8. 完成 bootstrap、terminal 与 normal shutdown，再进入 Batch D 集中建立 CPU visibility、single/multi-thread E2E、failure matrix 和真实 Vulkan smoke。

每阶段只能回滚尚未被下游正式依赖的完整批次；不得通过重新开放旧正式入口进行局部回滚。
