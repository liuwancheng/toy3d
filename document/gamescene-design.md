# Toy3d GameScene 与 Game/Render 边界设计

## 1. 状态与目标

Game Thread 到 Rendering Thread 的正式边界统一由 OpenSpec change
`establish-game-render-framework` 定义。本文第 5～7 节只保留 GameScene 调用侧摘要；线程、RenderCommand、
SceneProxy、frame fence 或 RHI 资源命令语义以该 change 的 capability specs 与 design 为准。

本文取代已归档 `archive/rendering-engine-foundation-design.md` 中由 `World` 帧末扫描并生成
`RenderSceneUpdateBatch`、由 `RenderResourceUpdateCollector` 全局遍历资源，以及
`CameraViewportFrameBuilder` 从 `CameraComponent` 直接构建渲染帧的旧 GameScene 方案。
历史施工记录只保留为事实记录，不再代表长期接口。

GameScene G1 建立清晰的对象层级、所有权、注册和 Transform 生命周期；G1.5 在不接入
Rendering Thread 的前提下补充 World/Actor 运行时生命周期、Actor Tick 与帧内安全销毁。
Game Thread 到 Rendering Thread 的 RenderCommand bridge 在下一独立批次实现。迁移期间不保留
collector/builder 兼容入口，也不让 `World` 暂时承担 RenderScene diff 策略。

目标：

- `World` 独占 Actor，并提供唯一正式 `spawn_actor<T>()` / `destroy_actor()` 入口；
- `Actor` 独占 ActorComponent，并可指定一个自己拥有的 SceneComponent 作为 root；
- `ActorComponent` 表达注册生命周期，`SceneComponent` 表达 attachment 与 Transform；
- `PrimitiveComponent` 表达可渲染空间对象，`StaticMeshComponent` 是首个实现；
- `StaticMeshActor` 组合一个默认 `StaticMeshComponent` 并将其设为 root；
- `World` 显式表达 initialize、begin play、Actor Tick 与 end play 边界；
- 只有显式启用的 Actor 参与 Tick，ActorComponent 不提供通用 Tick；
- Tick 或生命周期回调中销毁 Actor 时先标记 pending，安全点再执行 EndPlay、注销与释放；
- GameScene 不包含 RenderScene、RHI、ViewportFrame 或后端类型；
- 组件注册和变更通过 UE 式 RenderCommand 推送，不扫描整个 World。

非目标：

- 不复制 UE 的 UObject、反射、CDO、Blueprint、Level、GC 或 replication；
- 本文不重复定义 Rendering Thread、RenderCommand、SceneProxy 或帧同步实现；
- 本批不实现 Character、Pawn、Controller、GameMode、Physics 或通用 component tick；
- 不把 Asset/Material/StaticMesh 资源发布策略塞进 World。

## 2. 目录与类型层级

```text
engine/runtime/gamescene/
├── world/
│   ├── world.*
│   └── world_types.h
├── actor/
│   ├── actor.*
│   └── static_mesh_actor.*
└── component/
    ├── actor_component.*
    ├── scene_component.*
    ├── primitive_component.h
    ├── static_mesh_component.*
    ├── camera_component.*
    └── light_component.*
```

```text
ActorComponent
└── SceneComponent
    ├── PrimitiveComponent
    │   └── StaticMeshComponent
    ├── CameraComponent
    └── LightComponent

Actor
└── StaticMeshActor
```

未来有稳定用例后再增加 `CameraActor`、各类 `LightActor`、`Pawn` 与 `Character`；
不为目录整齐预建空类型。

## 3. 所有权与生命周期

```text
World
└── unique_ptr<Actor>
    └── unique_ptr<ActorComponent>
        └── non-owning attachment links between SceneComponents
```

- `World::spawn_actor<T>()` 先完成 Actor 构造，再纳入 World 所有权，最后注册全部组件；
- Actor 构造期间可创建默认组件，但此时组件尚未注册；
- 已注册 Actor 新增组件时立即注册该组件；
- 已初始化或已经 begin play 的 Actor 新增组件时，组件立即追平 owner 的生命周期；
- `destroy_actor()` 先逆序注销组件，再删除 Actor；
- Playing World 中销毁 Actor 时先执行 Actor/Component EndPlay，再逆序注销组件；
- Tick 与生命周期回调期间的销毁只标记 pending，当前安全阶段结束后统一释放；
- Actor 只能把自己拥有的 SceneComponent 设为 root；
- attachment 可以跨 Actor，但必须位于同一 World，关系不表达所有权；
- 父 Transform 变化立即向后代传播，World 不再每帧扫描所有组件更新 Transform；
- 父组件销毁时子组件脱离，优先保持 world transform，无法表示为 positive-scale TRS 时退化为保持 relative transform并记录诊断。

## 4. UE 参考与 Toy3d 取舍

采用 UE4.27 的职责划分：

- `UWorld` 负责 Actor 生命周期；
- `AActor` 以 `RootComponent` 表达空间身份；
- `UActorComponent::RegisterComponent` 建立运行期注册状态；
- `UPrimitiveComponent` 在注册、Transform 或动态数据变化时通知 Scene；
- `AStaticMeshActor` 只是一个拥有默认 `UStaticMeshComponent` root 的便利 Actor。

Toy3d 不复制 UObject subobject、construction script 或 UE 宏。注册生命周期保持直接的 C++ composition；
PrimitiveComponent 的正式 render-state contract 见第 5 节与 OpenSpec capability
`game-render-framework/renderer-scene-ownership`、`game-render-framework/primitive-proxy-lifecycle`。

### 4.1 World、Actor 与 Component 运行时生命周期

`WorldLifecycleState` 第一版只表达 `Created`、`Initialized` 与 `Playing`。`end_play()` 从
`Playing` 返回 `Initialized`，World 可以继续持有已经结束 play 的 Actor；World 析构时若仍在
Playing，必须先完成一次 end play。

```text
World::spawn_actor<T>()
    -> Actor/Components register
World::initialize()
    -> Components initialize
    -> Actor initialize
World::begin_play()
    -> Components begin play
    -> Actor begin play
World::tick(delta)
    -> only explicitly enabled Actor tick
World::end_play()
    -> Actor end play
    -> Components end play in reverse order
```

Transform、Physics、Animation、Particle 与 Audio 等领域更新不通过通用 Component Tick
驱动。以后确有独立 Gameplay Component Tick 用例时，再设计显式 opt-in contract；不得提前
让所有 ActorComponent 携带 Tick 状态。

`World::tick()` 接收 composition root 提供的有限且非负 delta time，并生成单调的
`WorldTickContext`。World 不直接读取平台时钟。Tick 开始时固定本帧 Actor 数量，因此 Tick
期间新 spawn 的 Actor 可以完成 register/initialize/begin play，但从下一帧才参与 Tick。

## 5. Game → RenderCommand 边界

GameScene 不直接依赖 RenderScene 实现。RenderCore 定义小型 `SceneInterface`，稳定 `Scene` 自身实现该接口；
Game Thread 方法只 enqueue，Render Thread 方法才修改场景状态：

```text
PrimitiveComponent register
    -> Component::create_scene_proxy()
    -> SceneInterface::add_primitive(unique_ptr<SceneProxy>)
    -> enqueue RenderCommand
    -> Rendering Thread creates PrimitiveSceneInfo and owns SceneProxy

Transform change
    -> SceneInterface::update_primitive_transform(SceneProxy*, owned transform/bounds)
    -> enqueue RenderCommand
    -> Rendering Thread updates Proxy and merges expensive Scene work

PrimitiveComponent unregister
    -> clear Component non-owning SceneProxy*
    -> SceneInterface::remove_primitive(SceneProxy*)
    -> enqueue RenderCommand
```

约束：

- SceneProxy 是 Component 的类型化 Rendering Thread 镜像，不是 snapshot wrapper；
- Component 在 Game Thread 创建 CPU-only Proxy 并保留 non-owning `SceneProxy*`，Scene/SceneInfo 使用 RAII 独占拥有；
- Proxy 复制渲染数据，不保存可由 Rendering Thread 解引用的 Actor、Component 或 World 指针；
- Scene 结构相关更新走 SceneInterface；纯 Proxy-local 更新可使用明确的 `*_game_thread()` enqueue façade；
- RenderCommand payload 使用 owned value、转移所有权或 immutable render data，稳定 Scene/Proxy 指针只作为受 drain contract
  保护的 identity；
- `World` 不遍历所有 Actor 来发现资源或渲染变化；
- `StaticMeshComponent` 保留 `StaticMeshRef` 作为 Game Thread 资产引用；StaticMesh 保存可共享的
  CPU 几何、local bounds、section 与默认 Material slot，不能由 Component 或 RHI handle 取代；
- `set_local_transform()`、`set_static_mesh()`、Material override 与 Light setter 是变化入口并立即 enqueue，不在
  Game Thread 建立统一 frame-local render batch；
- StaticMesh/Material 的 RHI 初始化与释放走 RenderResource 自己的命令生命周期，不建立全局 collector；
- CameraComponent 只保存相机属性，View 由 GameViewport、Player 或 Editor viewport 根据输出尺寸构建；
- RenderCommand 是高层 CPU 命令，RHI command list 和 GPU command buffer 是后续两层。

## 6. 帧同步约束

Rendering Thread 采用 UE 风格全局 enqueue façade，并直接复用 Task Graph：

- Rendering Thread attach `NamedThread::RenderingThread` 并只 pump Task Graph named queue；
- 引擎初始化阶段保证 RenderingThread/Renderer/RHI 已建立后才开放 RenderCommand；
- 第一阶段只有 Game Thread 可以提交正式 RenderCommand，错误线程 fail fast；
- multi-thread 模式进入 RenderingThread named queue，single-thread 模式在 Game Thread 立即执行相同 callable；
- 每个全局帧末插入 tracked frame fence；允许 one-frame lag 时双 fence 轮转，提交 N 后等待 N-1；
- Game Thread 通过 `TaskGraphInterface::wait_until_task_completes()` 等待并 helping，不另建条件变量队列；
- RenderCommand/frame fence 只表示 Rendering Thread CPU 工作到达完成点；
- GPU 完成继续由 `RHIQueueCompletionValue`、viewport frame slot 和 backend fence 表达；
- flush、renderer shutdown 和 device teardown 是显式强同步边界，正常逐帧不等待 GPU idle；
- 普通 RenderCommand 为 `FireAndForget`；只有 RenderCommandFence 等显式同步任务使用 GraphEvent completion。

## 7. 迁移批次

### G1：GameScene 结构与生命周期

- 建立 `world/`、`actor/`、component hierarchy；
- 加入 `spawn_actor<T>()`、组件注册和 `StaticMeshActor`；
- Transform 改为变更时传播；
- 删除 collector、camera builder、World render diff 与相应测试入口。

### G1.5：World/Actor 运行时生命周期

- 增加 World initialize、begin play、Actor-only Tick 与 end play；
- ActorComponent 参与 initialize/begin/end play，但不提供通用 Tick；
- Tick 中新 Actor 从下一帧参与 Tick，销毁 Actor 在帧内安全点统一释放；
- World 只累计调用方提供的 delta、world time 与 frame number，不建立平台时钟；
- 不增加 RenderCommand、SceneInterface、RenderDirty、Render ID 或 Rendering Thread 依赖。

### G2：Render SceneInterface 与组件 render state

- 实现 RenderCore `SceneInterface` 与类型化 SceneProxy contract；
- PrimitiveComponent 在 Game Thread 创建 Proxy，注册/注销生成 add/remove closure；
- transform/state/dynamic data 立即 enqueue，Render Thread 合并昂贵 Scene work；
- SceneInfo/Scene 使用 RAII 独占 Proxy，Component 只保留 non-owning 指针。

### G3：Rendering Thread 与 RenderCommand stream

- composition root 创建 RenderingThread Runnable；
- 全局模板 façade 直接使用 Task Graph named queue，替换 `RenderFrameDispatcher` 的 `std::thread`、私有 queue 和 completion；
- 实现双 frame fence 与 single-thread fallback；
- 完成启动、flush、fatal、shutdown 与窗口生命周期测试。

### G4：Viewport/View 与资源命令

- GameViewport/Editor viewport 负责 View 构建；
- CameraActor 仅提供默认 CameraComponent；
- RenderResource 通过独立 init/update/release command 接入 cache；
- 删除残留聚合 packet 中已经被 command stream 取代的字段。

## 8. 删除条件

G1 不保留以下旧入口：

- `World::create_actor()`；
- `World::update_transforms()`；
- `World::collect_render_scene_updates()`；
- `RenderResourceUpdateCollector`；
- `build_camera_viewport_frame()`；
- 空的旧 `gamescene.h/.cpp` 与 `camera/Camera` 占位类型。

后续每批必须保持唯一正式入口，不新增 snapshot collector 与 RenderCommand 双轨。
