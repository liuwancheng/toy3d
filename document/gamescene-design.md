# Toy3d GameScene 与 Game/Render 边界设计

## 1. 状态与目标

本文取代 `rendering-engine-foundation-design.md` 中由 `World` 帧末扫描并生成
`RenderSceneUpdateBatch`、由 `RenderResourceUpdateCollector` 全局遍历资源，以及
`CameraViewportFrameBuilder` 从 `CameraComponent` 直接构建渲染帧的旧 GameScene 方案。
历史施工记录只保留为事实记录，不再代表长期接口。

GameScene 第一阶段只建立清晰的对象层级、所有权、注册和 Transform 生命周期；
Game Thread 到 Rendering Thread 的 RenderCommand bridge 在下一独立批次实现。迁移期间不保留
collector/builder 兼容入口，也不让 `World` 暂时承担 RenderScene diff 策略。

目标：

- `World` 独占 Actor，并提供唯一正式 `spawn_actor<T>()` / `destroy_actor()` 入口；
- `Actor` 独占 ActorComponent，并可指定一个自己拥有的 SceneComponent 作为 root；
- `ActorComponent` 表达注册生命周期，`SceneComponent` 表达 attachment 与 Transform；
- `PrimitiveComponent` 表达可渲染空间对象，`StaticMeshComponent` 是首个实现；
- `StaticMeshActor` 组合一个默认 `StaticMeshComponent` 并将其设为 root；
- GameScene 不包含 RenderScene、RHI、ViewportFrame 或后端类型；
- 未来组件注册和变更通过 owned closure RenderCommand 推送，不扫描整个 World。

非目标：

- 不复制 UE 的 UObject、反射、CDO、Blueprint、Level、GC 或 replication；
- 本批不实现 Rendering Thread、RenderCommand queue、SceneProxy 或帧同步；
- 本批不实现 Character、Pawn、Controller、GameMode、Physics 或 component tick；
- 不把 Asset/Material/StaticMesh 资源发布策略塞进 World。

## 2. 目录与类型层级

```text
engine/runtime/gamescene/
├── world/
│   └── world.*
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
- `destroy_actor()` 先逆序注销组件，再删除 Actor；
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

Toy3d 不复制 UObject subobject、construction script 或并发后缀。注册生命周期保持直接的 C++
composition；RenderCommand bridge 出现后再为 PrimitiveComponent 增加稳定的 render-state contract。

## 5. 未来 Game → RenderCommand 边界

GameScene 不直接依赖 `RenderScene`。未来在 RenderCore 定义小型 `SceneInterface`，由 Rendering
Thread 所有的实现接收 Game Thread 调用并 enqueue owned closure：

```text
PrimitiveComponent register
    -> SceneInterface::add_primitive(owned snapshot)
    -> enqueue RenderCommand
    -> Rendering Thread creates PrimitiveSceneInfo / SceneProxy

Transform change
    -> coalesced end-of-frame component update
    -> SceneInterface::update_primitive_transform(id, owned transform/bounds)
    -> enqueue RenderCommand

PrimitiveComponent unregister
    -> SceneInterface::remove_primitive(id)
    -> enqueue RenderCommand
```

约束：

- closure 不捕获 Actor、Component、World 或临时对象裸指针；
- payload 使用 stable ID、值快照或明确共享所有权；
- `World` 不遍历所有 Actor 来发现资源或渲染变化；
- StaticMesh/Material 的 RHI 初始化与释放走 RenderResource 自己的命令生命周期，不建立全局 collector；
- CameraComponent 只保存相机属性，View 由 GameViewport、Player 或 Editor viewport 根据输出尺寸构建；
- RenderCommand 是高层 CPU 命令，RHI command list 和 GPU command buffer 是后续两层。

## 6. 帧同步约束

Rendering Thread 接入时采用 UE 风格但保持 Toy3d 的显式注入：

- Rendering Thread attach `NamedThread::RenderingThread` 并只 pump Task Graph named queue；
- 第一阶段只有 Game Thread 可以提交正式 RenderCommand，保持单 producer command stream；
- 每个全局帧末插入 tracked frame fence；允许 one-frame lag 时双 fence 轮转，提交 N 后等待 N-1；
- Game Thread 通过 `TaskGraphInterface::wait_until_task_completes()` 等待并 helping，不另建条件变量队列；
- RenderCommand/frame fence 只表示 Rendering Thread CPU 工作到达完成点；
- GPU 完成继续由 `RHIQueueCompletionValue`、viewport frame slot 和 backend fence 表达；
- flush、renderer shutdown 和 device teardown 是显式强同步边界，正常逐帧不等待 GPU idle；
- 任何已接受命令都必须发布 completion，错误结果在 completion release 前写入 owned result payload。

## 7. 迁移批次

### G1：GameScene 结构与生命周期

- 建立 `world/`、`actor/`、component hierarchy；
- 加入 `spawn_actor<T>()`、组件注册和 `StaticMeshActor`；
- Transform 改为变更时传播；
- 删除 collector、camera builder、World render diff 与相应测试入口。

### G2：Render SceneInterface 与组件 render state

- 先设计 RenderCore `SceneInterface`、render state ID 和错误语义；
- PrimitiveComponent 注册/注销生成 add/remove closure；
- Transform/state/dynamic data 在 Game Thread 帧末合并后生成 update closure；
- RenderScene 创建并独占 SceneInfo/Proxy。

### G3：Rendering Thread 与 RenderCommand stream

- composition root 创建 RenderingThread Runnable；
- 用 Task Graph named queue 替换 `RenderFrameDispatcher` 的 `std::thread`、私有 queue 和 completion condition variable；
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
