## Context

见 `proposal.md`。本文中的 `Engine` 始终特指 `engine/runtime/engine.h` 声明、`engine/runtime/engine.cpp` 实现的现有 `toy3d::Engine`，不表示新模块或新抽象层。当前该类已经事实拥有 Platform、Window、RHIDevice、viewport、旧 RenderResourceCache 和具体 SceneRendering，并在 Game Thread 直接 begin/render/end frame；仓库同时存在 Task Graph/RenderCommand 原型与旧 frame dispatcher。新设计保留底层 Task Graph named queue、RHI local state、queue completion、deferred deletion、viewport abort 和 bootstrap context，删除上层双轨并把 RHI/RenderScene 可变状态迁入 logical Rendering Thread。

本文件是多次 apply 的主导航文档。行为细节以 `specs/game-render-framework/**/spec.md` 为准；任何新增第一方具名类型都必须先登记在唯一所属子 Spec 的 `Type Contracts`。

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
└─ SceneView / ViewFamily 的一次性值构造

Bridge contracts（不拥有业务可变状态）
├─ RenderCore RenderCommand / RenderCommandFence
├─ SceneInterface
└─ RenderResource lifecycle façade

Render side（logical Rendering Thread 可变）
├─ RenderingThread / Renderer
├─ RenderScene / PrimitiveSceneInfo / PrimitiveSceneProxy
├─ SceneRenderer 执行与显式 passes
├─ RenderResourceManager / render representations
└─ public RHI / backend execution
```

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
  ├─ build ViewFamily
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
  │                                            ├─ query RenderScene / cull
  │                                            ├─ resolve materials + current views
  │                                            ├─ record explicit passes serially
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

每次 apply 只选择一个阶段或其中连续的小任务，不跨越未完成的直接前置。若实现发现新类型，先修改对应 Spec、运行 strict validation，再继续代码。

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

Proxy 在 GT 创建后 move 到 Add command；update 捕获 pointer identity 与 copied value；remove 先摘除索引后 RT 析构。稳定对象地址是同步 FIFO identity，不建立 Proxy ID/revision。GT 禁止解引用 opaque pointer。

`SceneView`、`ViewFamily`、`SceneRenderer` 是一次性输入，禁止以 `Snapshot` 命名。GT 构造时不得读取 RT RenderScene；SceneRenderer move 进 Draw 后由 RT 查询 FIFO 更新后的场景并析构。

### 6. RenderResourceManager 是 RT-only non-owning lifecycle manager

```text
Uninitialized → PendingUpload → Ready → Released
                         └────→ Failed
```

Manager 只维护 non-owning pending pointers 和本次 recording collection，不承担 Asset cache、registry、ownership、Scene/Material 规则或 backend allocation。`upload_*()` 返回前复制到 RHI staging，但 CPU initial payload 保留到 business submit 成功；abort/submit failure 仍可重录。

正常 release 先排队移除 Scene/Material 引用，再把 owning representation move 进最后一条 release command。RT 从 manager 移除、release RHI refs并析构；实际 native object 由 command list/in-flight refs保活到 completion，不 wait idle。

### 7. Mesh、Texture、Material 分开建模

StaticMeshRenderData 不继承 RenderResource；各 vertex/index buffer 独立继承，VertexFactory 是独立 RT object。整体 ready gate 防止部分资源 Draw，replacement 用完整 candidate RenderData。

TextureRenderResource 地址稳定，内部保存 active/candidate RHI texture/view。内容更新不更换 view；descriptor/format/mip replacement 只有在 submit 成功后发布并递增 RT-only binding generation。

MaterialRenderProxy 不继承 RenderResource。普通 setter 只更新参数表和 dirty；Draw 前按需生成 frame-local constants/binding。结构性 shader/layout/render-state 变化使用完整 candidate replacement。

### 8. RHI frame-end 的权威是业务 submit

```cpp
struct RHIFrameEndResult
{
    RHIQueueCompletionValue completion_value = 0;
    RHIStatus presentation_status;
};
```

外层 `RHIResult` 成功表示 business lists 已 submit；失败表示未 submit。所有 validation 前移到 native submit 前。submit 后的 state/resource commit、last-use、in-flight retain 不返回可恢复失败；如果内部不变量破坏，仍报告“已提交 + completion”并 latch terminal。

abort 的最小 submit 只闭合 acquire synchronization，不算业务 submit。Resize 只要求等待该 viewport in-flight completion；Vulkan `vkDeviceWaitIdle()` 可暂作 backend 简化实现，不成为公共 contract。

### 9. local state、committed state 与 completion 分层

录制 transition 只修改 command-list local tracker；queue 按实际 submit 顺序发布 committed state，无需等待 GPU。discard/submit failure 不发布 final state。Completion 只回收 allocator/staging/descriptor/list/native resource。

公共 RHI object 必须具有不可变 device ownership identity，在 native 调用前拒绝跨 device 混用。Vulkan/D3D12 显式 barrier；D3D11 维护逻辑 hazard，GPU completion 使用 FL11_0 event query 或等价机制。

### 10. Renderer bootstrap 与 shutdown 分开处理

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

### 11. Type Contracts 是 apply gate

每个具名类型只有一个 owning Spec。Type Contracts 必须说明：

- 类型名称及新增/现有修改状态；
- 稳定领域职责，禁止只为访问控制或模板形状制造 `Key/Token/Enabler/Storage`；
- CPU owner、non-owning observers 和 ownership transfer；
- mutable thread、create/init/release/destroy thread；
- 状态与错误语义；
- 不能复用现有类型的原因。

如果实现能使用局部变量、标准容器、直接值、函数或既有类型完成，就不得新增类型。需要新增时先修改子 Spec 并 strict validate；tasks 不授予绕过此门槛的权限。

## Risks / Trade-offs

- [阶段 0 删除旧 demo 路径会暂时减少可运行功能] → 每个下游 capability 尽快恢复纵向闭环，阶段门槛优先保持 build/test green，不保留 no-op adapter。
- [稳定裸指针依赖 FIFO 生命周期] → 只通过专用生命周期 API 排序，normal release 强制 ownership transfer，并覆盖悬空窗口测试。
- [单 active Task Graph 降低多实例灵活性] → 这是已确认 process model；保持显式 ownership/shutdown，未来改变产品模型时先改 Spec。
- [Type Contracts 增加规划维护成本] → 换取多次 apply 的命名稳定性，避免临时 wrapper 扩散；标准库和编译器生成类型豁免。
- [无 upload budget 造成首帧尖峰] → 第一阶段先闭合正确性并记录统计，真实 workload 后单独设计 streaming/budget。
- [单 command list 限制 CPU 录制并行] → 公共 end frame 保留 list vector和 context seam，状态接缝/RDG 未就绪前不提前并行。
- [submit 后错误不可回滚] → 前移 validation；提交后明确返回 completion并 terminal，优先保证生命周期真相。

## Migration Plan

1. 以已 strict validate 的本 change 和 `document/index.md` 作为唯一规划入口，从阶段 0 开始执行。
2. 按导航阶段 0 清理废弃路径，保留底层 Task Graph/RHI 基础。
3. 建立 Task Graph active instance、RenderingThread lifecycle、RenderCommand/Fence/FrameEndSync 与最小 Renderer shell。
4. 将这些已定型对象直接接入 `engine/runtime/engine.h/.cpp::toy3d::Engine`，移除该类对 frame/RHI 的直接执行，不新增第二层 Engine 抽象。
5. 建立 RenderScene/Proxy/View/SceneRenderer 纵向场景闭环。
6. 迁移 RHI frame submit/state/completion contract及 backend。
7. 建立 RenderResourceManager，再独立迁移 Mesh、Texture、Material。
8. 完成 bootstrap、terminal、normal shutdown 与 Editor 集成矩阵。

每阶段只能回滚尚未被下游正式依赖的完整批次；不得通过重新开放旧正式入口进行局部回滚。
