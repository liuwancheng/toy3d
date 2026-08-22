## Why

Toy3d 现有 Game/Render 传输、Engine 启动链、RenderScene、资源镜像和 RHI 帧提交来自多轮历史原型，缺少一份能够跨多次 apply 持续导航的总控规范。需要以 UE4.27 容易识别的职责划分重新建立框架，并把清理、composition root、线程、Scene、资源、RHI 和 terminal 生命周期拆成可独立验收的子 capability。

## What Changes

- 新增一个总控 `game-render-framework` capability，规定端到端一帧流程、子 capability 依赖、跨层不变量和分阶段完成门槛。
- 新增类型治理 contract：apply 期间新增的每个第一方 class、struct、enum、alias 或其他具名类型，MUST 预先登记在所属子 Spec 的 `Type Contracts` 中，说明职责、所有权、线程、生命周期与错误语义；未登记类型不得直接实现。
- **BREAKING**：先删除旧 frame packet/dispatcher/queue/completion、旧 RenderCommand 原型、旧资源 cache/ID/revision 和相应正式测试，不保留新旧双轨。
- 将 `engine/runtime/engine.h` 中现有 `toy3d::Engine` 收敛为 Game Thread composition root，显式拥有并编排 Task Graph、RenderingThread controller、Renderer shell、Window/Surface 与 Game loop；不新增第二层 Engine 抽象，RHI/RenderScene 可变状态转移到 logical Rendering Thread。
- 除具体 `toy3d::Engine` 外，其余框架职责按 Game side 与 Render side 分离；RenderCore bridge 只保存跨侧 contract，不拥有 Game 或 Render 业务可变状态。
- **BREAKING**：Task Graph 改为 composition root 拥有的进程级唯一 active instance，并提供受生命周期约束的访问入口。
- 建立无 RHI 参数的 move-only RenderCommand、RenderingThread、RenderCommandFence、FrameEndSync 和 single-thread 等价路径；不引入 RHI Thread、route、registry 或 `RHICommandListImmediate`。
- 建立 Renderer-owned RenderScene、World/SceneInterface、PrimitiveSceneProxy 与一次性 SceneRenderer 的 UE 风格边界。
- **BREAKING**：建立 RT-only、non-owning RenderResourceManager，分别规范 StaticMesh、Texture 与 Material 的 init/update/replacement/release。
- **BREAKING**：RHI frame-end 分离业务 submit 与 presentation status，并规范 local/committed state、completion、abort 和 deferred deletion。
- 定义 renderer bootstrap、placeholder、viewport、terminal、drain 和 shutdown 顺序。
- 第一阶段保持单 graphics queue、单线程录制和每 viewport Draw 一个 graphics command list；streaming、upload budget、pass 并行、RDG、async compute 与完整 headless Renderer 后置。

## Capabilities

### New Capabilities

- `game-render-framework`: 总控 contract、类型治理、端到端一帧流程和子 capability 完成门槛。
- `game-render-framework/legacy-rendering-cleanup`: 旧 transport、资源 cache/ID/revision 与废弃测试的删除边界。
- `game-render-framework/engine-composition-root`: 现有 `engine/runtime/engine.h::toy3d::Engine` 对 Task Graph、RenderingThread、Renderer、Window/Surface 和 Game loop 的所有权及初始化顺序。
- `game-render-framework/task-graph-runtime`: 进程级 active Task Graph、named thread、访问入口与 shutdown。
- `game-render-framework/render-command-transport`: move-only RenderCommand、FIFO、producer 与 façade 边界。
- `game-render-framework/rendering-thread-lifecycle`: multi/single-thread 的启动握手、pump、return 与 join。
- `game-render-framework/frame-synchronization`: RenderCommandFence、FrameEndSync、frame lag、flush 与只读 terminal 传播。
- `game-render-framework/renderer-scene-ownership`: Renderer、World、SceneInterface 与 RenderScene 的所有权边界。
- `game-render-framework/primitive-proxy-lifecycle`: PrimitiveSceneProxy add/update/remove、stable identity 与析构顺序。
- `game-render-framework/view-render-flow`: SceneView、ViewFamily、SceneRenderer、Draw command 与一帧 RT 执行流程。
- `game-render-framework/render-resource-manager`: RenderResource 状态、pending upload transaction、提交后发布与释放。
- `game-render-framework/static-mesh-resources`: Mesh buffer、VertexFactory、整体 ready gate 与 replacement。
- `game-render-framework/texture-resources`: 稳定 Texture representation、内容更新、candidate replacement 与 binding generation。
- `game-render-framework/material-updates`: MaterialInstance、MaterialRenderProxy、普通 setter 与结构性 replacement。
- `game-render-framework/rhi-frame-submission`: begin/end/abort、业务 submit、present 与跨后端 frame contract。
- `game-render-framework/rhi-resource-state`: command-list local state、committed state、completion 与 deferred deletion。
- `game-render-framework/renderer-bootstrap`: RT device、resource manager、placeholder、viewport 初始化及失败回滚。
- `game-render-framework/renderer-terminal-shutdown`: first-error latch、terminal cleanup、normal drain 和 device-lost teardown。

### Modified Capabilities

当前 `openspec/specs/` 中没有需要修改的既有 capability。

## Impact

- 受影响模块：具体 `engine/runtime/engine.h/.cpp`、Core Task Graph、RenderCore bridge、GameScene、RenderScene、公共 RHI、Vulkan backend、runtime CMake 与相关测试；不新增 Engine 模块或第二个 Engine 类型层级。
- 旧 Game/Render transport、资源 cache/ID/revision、空壳 device command list 和 Engine 直持 RHI/SceneRendering 的路径将被迁移或删除。
- RHI 公共接口仍须可由 Vulkan、D3D11 FL11_0、D3D12 和 `VulkanPortable v1` 实现。
- 本 change 取代此前粗粒度规划，并成为 `document/index.md` 指向的唯一执行入口。
