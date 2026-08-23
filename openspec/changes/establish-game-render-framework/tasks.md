## 1. Apply Governance

- [x] 1.1 在每次 apply 开始时读取主 design、目标子 Spec 与直接前置完成状态，并在本次交付中记录选择依据。
- [x] 1.2 在新增任何第一方具名类型前核对目标 Spec 的 Type Contracts；缺失时先更新 Spec 并以 `openspec validate establish-game-render-framework --strict` 验证。
- [x] 1.3 从下一未完成 capability 起采用批次验证：Batch B/C 中间阶段只执行 CMake configure、受影响正式 target build 和少量跨模块 smoke；完整单元、failure matrix、single/multi-thread E2E 与真实 Vulkan smoke 集中到 Batch D，不再要求每个框架空壳单独建立大测试 fixture。
- [x] 1.4 每个新增或修改模块 MUST 标明 Game side、Render side 或 stateless bridge，并通过依赖审查验证 Game side 无 RenderScene/RHI、Render side 无 Game 对象回读。
- [x] 1.5 在对应代码 Batch 开始前，为仍涉及新增或修改运行时代码的 capability spec 补齐 `Minimal Implementation Example`，只使用已确认名称，并覆盖 owner/observer/ownership transfer、GT/RT mutable thread、主调用顺序和至少一个失败路径。
- [x] 1.6 新增具名类型时逐项记录 UE4.27 对应术语、Toy3d 实际职责、不能复用既有类型的原因和用户确认结果；未登记到 Type Contracts 前不得实现或写入正式测试接口。

## 2. Legacy Rendering Cleanup

- [x] 2.1 生成旧 frame transport、Scene frame processor、resource cache/collector、typed ID/revision、空壳 command list 的定义/调用/测试/CMake 清单，并用 `rg` 验证覆盖全部正式入口。
- [x] 2.2 删除旧 `RenderFramePacket/Queue/Dispatcher/Completion` 源码和 CMake/test target，并验证旧 transport 符号只剩 archive/OpenSpec 引用。
- [x] 2.3 删除旧 Scene update batch、frame processor 和 Camera frame builder 正式路径，并验证 GameScene/RenderScene 不再生成旧 frame packet。
- [x] 2.4 删除 `RenderResourceCache`、collector、render-resource typed ID/revision 与对应 cache/upload pipeline tests，并验证旧 identity 符号无运行时残留。
- [x] 2.5 删除空壳 `RHIDeviceCommandList`、带该参数的 RenderCommand 原型及原型测试，验证公共 RHI 不再包含该类型。
- [x] 2.6 调整 Engine 临时启动路径使删除后仍可配置和安全退出，不增加 no-op 成功 adapter，并构建 `Toy3dEditor` 验证阶段 0 build green。
- [x] 2.7 运行保留的 Task Graph、RHI upload/state/viewport tests，验证清理未删除底层可复用能力。

## 3. Task Graph Runtime

- [x] 3.1 复核 `task-graph-runtime` Type Contracts，确认本批除修改 `TaskGraphInterface` 外不新增类型，并 strict validate。
- [x] 3.2 为 Task Graph 增加 composition-root-owned active publication 与 `is_running()`，验证启动前/运行中/关闭后三态测试。
- [x] 3.3 实现受控 `TaskGraphInterface::get()`，验证未运行访问、正常访问和 shutdown race 诊断。
- [x] 3.4 实现重复 active instance 启动拒绝和首次 instance 保持，验证两个 factory/start 竞争测试。
- [x] 3.5 核对 GameThread/RenderingThread attach 与 same-producer FIFO，补充尚未 attach target 的拒绝测试。
- [x] 3.6 核对显式 shutdown、tracked completion 和 waiter wake，运行完整 Task Graph test target。

## 4. RenderingThread Lifecycle

- [x] 4.1 按 Type Contracts 新增 `RenderingThread` 与 `RenderingThreadMode`，验证头文件独立包含、所有权和线程注释。
- [x] 4.2 实现 multi-thread OS thread 创建与 Task Graph RenderingThread attach，验证 thread id 和 named-thread TLS。
- [x] 4.3 实现 ready handshake，使 attach/bootstrap result 发布前普通 façade 保持关闭，并验证提前 enqueue 被拒绝。
- [x] 4.4 实现 named queue pump、wake 和 return request，不新增私有 queue，并验证 idle/wake/return 测试。
- [x] 4.5 实现 single-thread controller，使 logical RT 映射 GT 且不创建 OS thread，并验证生命周期线程记录。
- [x] 4.6 实现 start failure 的 stop/join cleanup，覆盖 thread create、attach 和 bootstrap callback 失败注入。
- [x] 4.7 实现 teardown 后 request return/join 顺序，运行 repeated multi/single start-stop 测试。

## 5. RenderCommand Transport

- [x] 5.1 按 Type Contracts 新增内部 `RenderCommandTask<Callable>`，验证 move-only callable、`void() noexcept` 编译期约束。
- [x] 5.2 实现 GT multi-thread enqueue 到 RenderingThread named queue，验证命令名、FIFO 和 FireAndForget 行为。
- [x] 5.3 实现 logical RT inline 路径，验证嵌套 RenderCommand 不重复排队且保持顺序。
- [x] 5.4 实现 GT single-thread inline 路径，验证与 multi-thread 使用相同 callable body。
- [x] 5.5 实现 AnyWorker producer fail-fast 和未启动/关闭后 enqueue 诊断，覆盖所有非法 producer 状态。
- [x] 5.6 建立内部 façade enable/disable 生命周期，不暴露 Binding/Route/Registry/Renderer getter，并用公共头文件审查验证。
- [x] 5.7 运行 move-only ownership、terminal disposal thread 和高数量 FIFO 压力测试。
- [x] 5.8 将现有 `enqueue_render_command()` 从返回 `TaskGraphStatus` 收敛为无返回值 fire-and-forget API；正常返回保证同步执行或 transport ownership 已接受，迁移现有调用点和返回值断言，并只保留一个代表性 contract-violation 检查，不扩展独立测试矩阵。

## 6. Frame Synchronization

- [x] 6.1 按 Type Contracts 新增 `RenderCommandFence`、`FrameEndSync`、`RenderFenceWaitResult`，验证无 GPU fence/RHI ownership。
- [x] 6.2 实现 tracked Fence task 和 GT wait/helping，验证 Fence 前序 callable 全部完成。
- [x] 6.3 验证 Fence 完成不等待 GPU completion，使用可控 fake queue 分离两种信号。
- [x] 6.4 实现双 Fence one-frame lag 和 zero-lag 配置，验证连续帧最大领先量。
- [x] 6.5 实现 explicit flush rendering commands，并验证普通 setter/init 不隐式调用 flush。
- [x] 6.6 实现 framework failure/renderer terminal 的 waiter wake 与只读结果传播，覆盖等待竞态测试。

## 7. Renderer and Scene Ownership

- [x] 7.1 按 Type Contracts 新增最小 `Renderer` shell、`SceneInterface`、`RenderScene`，验证模块依赖方向和头文件无 backend 类型。
- [x] 7.2 实现 Renderer shell GT 创建、logical RT 初始化/teardown 和稳定地址，验证 GT 不读取内部可变状态。
- [x] 7.3 实现 Renderer-owned RenderScene 创建销毁，验证 RT-only mutation thread assertions。
- [x] 7.4 实现 World non-owning SceneInterface bind/unbind，验证 World 不包含或拥有 RenderScene。
- [x] 7.5 将 SceneInterface 正式收敛为无返回值 fire-and-forget 的 `add_primitive(std::unique_ptr<PrimitiveSceneProxy>)`、`update_primitive_transform(PrimitiveSceneProxy*, Matrix4, AxisAlignedBounds, bool)`、`remove_primitive(PrimitiveSceneProxy*)`，迁移已实现 `bind_scene()`/`unbind_scene()` 调用链并用 `rg` 验证不暴露 RT query、RHI 或 Renderer getter。
- [x] 7.6 完成 bind 期间现有 Primitive render state 补建顺序、unbind 先 destroy render state、composition-root 受控 shutdown 投递窗口、Fence drain 与 RenderScene 后 teardown 顺序；本阶段只复用一个 lifecycle smoke，不扩展独立场景测试矩阵。

## 8. Batch B — Game / Scene / View Framework

- [x] 8.1 直接在现有 `engine/runtime/engine.h/.cpp::toy3d::Engine` 接入 Platform/Window/RHISurface、Task Graph、Renderer、RenderingThread 与 FrameEndSync composition 顺序，移除 Engine 对具体 RenderScene/RHI frame 执行的直接 ownership，不新增第二个 Engine wrapper 或 service locator。
- [x] 8.2 实现 `PrimitiveSceneProxy`、`StaticMeshSceneProxy`、`PrimitiveSceneInfo` 与 RenderScene registration；StaticMeshSceneProxy 只保存 copied transform/visibility/world bounds 和受 FIFO 保护的 StaticMeshRenderData/MaterialRenderProxy non-owning references，不回读 Game 对象。
- [x] 8.3 实现 PrimitiveComponent 的 `create_render_state()`、`send_render_transform()`、`destroy_render_state()`，覆盖 registration/bind 补建、world transform 先更新 bounds、按值投递 Matrix4/AxisAlignedBounds/visible、Add 正常返回保证 transport ownership 已接受、正常 Remove 先摘除后析构，以及 terminal 竞争窗口中 Add/Remove 业务体 skip、opaque identity 不解引用和 payload 在 logical RT 析构；StaticMesh或material-slot identity变化通过destroy→create重建，MaterialInstance内部参数值变化不重建Primitive render state。
- [x] 8.4 实现共享 math `Plane`、`ConvexVolume` 与 left-handed、0..1 reversed-Z frustum 提取；finite 使用六面，infinite-far 使用五面，AABB 接触平面保持可见，不引入空间索引类型。
- [x] 8.5 实现 GT 一次性 `SceneView`、`SceneViewFamily`，确保复制 Camera/viewport/projection values 且不保存 CameraComponent、Window、World 或 RenderScene 可变引用。
- [x] 8.6 实现 `ViewInfo`、一次性 `SceneRenderer`、`ForwardSceneRenderer` ownership；Draw command 独占 SceneRenderer 并保证正常执行与 terminal skip/disposal 都在 logical RT 析构。
- [x] 8.7 实现 `ForwardSceneRenderer::init_views()` 的 CPU per-view 阶段：校验 view rect/output/near/projection mode 与输入/派生矩阵有限性，构造 view、reversed-Z projection、view-projection、inverse matrices 和 ConvexVolume，并为每个 View 重置独立本帧可见状态；失败时返回可诊断结果且不录制后续业务 pass，本任务不 acquire、查询或 abort viewport frame。
- [x] 8.8 实现 `compute_view_visibility()` 对 RenderScene 中 PrimitiveSceneInfo 的逐 View 线性 AABB 剔除，排除 disabled/invalid/removed Proxy，不实现 octree、occlusion、distance culling、LOD 或跨帧 cache。
- [x] 8.9 建立 visible StaticMeshSceneProxy→frame-local `MeshBatch` 收集骨架，只依赖 StaticMeshRenderData 整体可绘制 gate；暂不引入任何尚未完成名称确认的 mesh processing、draw-command、scene-texture 或收集接口类型。
- [x] 8.10 中间验证仅执行推荐 Windows CMake configure、`Toy3dEditor` Debug build，以及一条覆盖 World bind/unbind、Proxy add/remove、SceneRenderer terminal disposal 的 lifecycle smoke；记录不运行完整 `ctest` 的批次理由。

## 9. Batch C1 — Shader / RHI Vertex Input

- [x] 9.1 修改 ShaderMap runtime metadata，使 `ShaderMapEntryLoader` 完整保留 vertex-stage `ReflectedInterfaceVariable`，拒绝重复 logical attribute、unsupported shape、缺失 target mapping 和冲突 Program input；禁止根据 Shader 名或 hard-coded location 猜测。
- [x] 9.2 按 Type Contracts 实现 `ShaderVertexAttributeId`、`ShaderVertexInput`，第一阶段固定 POSITION0、NORMAL0、TEXCOORD0 与 optional COLOR0 logical attributes，并保持跨 target parity 不比较 native slot/location。
- [x] 9.3 按 Type Contracts 实现 `RHIShaderVertexInputReflection` 并接入 `RHIShaderDesc`、descriptor validation、shader/pipeline cache key/equality，完整携带 semantic name/index、location、scalar type和 component count。
- [x] 9.4 将 graphics pipeline vertex layout 与 Shader reflection compatibility validation 前移到公共 RHI；重复 location/semantic、unsupported format/shape、layout 不匹配必须在 backend native creation 前失败。
- [x] 9.5 Vulkan backend 使用 location/format 建立 native vertex-input state；保留 D3D11 FL11_0/SM5 semantic input-layout 和 D3D12 semantic/PSO 的可实现 contract 与测试 seam，不把 native 类型暴露到公共头文件。
- [x] 9.6 实现不继承 RenderResource 的 `VertexFactory`、`LocalVertexFactory` 和 `VertexStreamComponent` matching；VertexFactory 不选择 Material/Shader/permutation，不引入全局 registry、独立 permutation domain、manual vertex fetch 或 GPU Scene。
- [x] 9.7 中间验证只运行一个 ShaderMap vertex metadata conversion smoke、一个 LocalVertexFactory compatibility smoke，以及 `Toy3dEditor`/直接受影响 Shader target build；不运行跨后端/full renderer 测试矩阵。

## 10. Batch C2 — Resource / Material / Forward Base Pass

- [x] 10.1 完成 viewport business submit result、submit/present/abort contract：submit success 后即 commit，present Suboptimal/OutOfDate/terminal 不回滚；明确未产生 GPU work 的 submit failure discard，执行边界未知时锁存 terminal。
- [x] 10.2 完成 command-list local first/current/final access、按实际 submit 顺序的 queue committed state、device ownership identity 和 completion-driven payload 回收；支持同一 list upload→transition→draw，D3D11 completion 使用 FL11_0 event query contract。
- [x] 10.3 实现 `RenderResource`、`RenderResourceState`、`RenderResourceManager` 的 RT-only pending collection、`record_pending_uploads()`、当前 recording 局部可用、submit 后 Ready、abort/retry、release 与 terminal clear；Manager 保持 non-owning。
- [x] 10.4 实现 `StaticMeshRenderData`、`PositionVertexBuffer`、`StaticMeshVertexBuffer`、optional `ColorVertexBuffer`、`StaticMeshIndexBuffer` 与 `LocalVertexFactory` 的完整 candidate gate；当前只支持单组 geometry/sections，不实现 LOD/streaming/partial residency。
- [x] 10.5 在 `engine/core/pixel_format/` 建立独立 `Toy3dPixelFormat` CMake target 和共享 `PixelFormat` Type Contract 实现，提供跨 runtime/editor/tools/RHI 的 GPU-ready format 与 block width、block height、bytes-per-block metadata；不得依赖 Asset、RenderScene、公共 RHI 或 backend。
- [x] 10.6 将公共 descriptors、RenderCore geometry、Vulkan backend 与全部调用点从 `RHIFormat` 迁移到共享 `PixelFormat`，并在同一批次删除 `RHIFormat`；D3D11/D3D12/Vulkan native 转换保持 backend-local 且不得依赖枚举数值相同。
- [x] 10.7 更新 `document/rhi-design.md`、`document/core-module-usage-index.md` 与 `.codex/skills/design-rhi/references/toy3d-rhi-requirements.md`，记录 `Toy3dPixelFormat` 的用途/非目标、目录和 target、所有权/线程/错误边界、backend mapping、Editor/Cook source 分层、测试矩阵与迁移删除条件。
- [x] 10.8 使用 `rg` 检查正式代码和文档中无 `RHIFormat` compatibility alias、格式枚举数字强转、语义重复的 `TextureFormat`/第二套 GPU-ready format；验证公共头文件不包含 `VkFormat`、`DXGI_FORMAT` 或 Editor source encoding。
- [x] 10.9 完成 CMake configure、直接受影响正式 targets build、格式/压缩 block pitch 定向测试与 format capability validation smoke，并核对 Vulkan、D3D11 FL11_0、D3D12、`VulkanPortable v1` 映射可实现性。
- [x] 10.10 实现 `Texture`、`TextureDesc`、`TextureRef` 与 `TextureResource`，覆盖 stable address、GPU-ready `PixelFormat` payload validation、initial upload、active/candidate replacement、内容更新不换 view、submit 后 binding generation 和旧 view 到 completion 保活；`TextureDesc` 不保存 RHI usage，第一阶段 `TextureResource` 固定构造单采样 `ShaderResource | CopyDestination` RHI Texture2D 并显式 transition，RenderTarget、DepthStencil 与 Storage texture 不进入 Asset Texture 路径。
- [x] 10.11 修改 MaterialInstance 并实现 `MaterialRenderProxy`：scalar/vector/texture setter type validation 与 FIFO、Texture 新强引用先建立、RT dirty state、可见 Draw 前按需物化、binding generation 失效和完整结构性 candidate replacement。
- [x] 10.12 按 `view-render-flow` 与 `primitive-proxy-lifecycle` Type Contracts 实现 `ViewUniformShaderParameters` 与 `PrimitiveUniformShaderParameters` 的 canonical matrices/camera/object transform 数据；从 8.7 已验证的 ViewInfo 和 copied Proxy values 初始化，分别归属 View/Object logical Binding Group，不在 Shader 或 Vulkan 上层手写平台翻转。
- [x] 10.13 完成 `MeshBatch` 对 section range、StaticMeshRenderData、LocalVertexFactory 与 MaterialRenderProxy 的 frame-local non-owning 组合；invalid section、不可绘制 gate、ShaderVertexInput 不兼容或 Material binding 缺失时诊断并跳过对应 batch。
- [ ] 10.14 完整实现 `ForwardSceneRenderer::render_base_pass()`：逐 View 消费可见 MeshBatch，解析 Global/View/Pass/Material/Object `RHIGraphicsBindings`，建立兼容 pipeline、设置 viewport/scissor 和 vertex/index buffers，并录制 indexed draw。
- [ ] 10.15 使用现有 `RHIViewportContext` contract 闭合外层 frame ownership 和同一业务 list：`begin_frame()`→`record_pending_uploads()`→`init_views()`→visibility→MeshBatch→Base Pass→finish→submit/present；begin 成功后若 `init_views()` 或后续录制前置失败，外层 frame owner MUST 跳过业务 pass 并调用 `abort_frame()`，不得把 viewport ownership 下沉到 `init_views()`；submit success 后再发布 RenderResource Ready/RHI committed state，GPU completion 只控制保活回收。该任务消费既有 viewport interface，primary viewport 的创建、长期 ownership 与 Running publication 仍由 12.1 接入。
- [ ] 10.16 中间验证仅运行直接受影响正式 target build、既有 RHI state/upload smoke，以及一条资源 upload+Base Pass command-recording smoke；failure matrix、single/multi-thread E2E 与真实 Vulkan 多帧留到 Batch D。

## 11. Batch C3 — Render-side Test Pass

- [ ] 11.1 盘点并迁移现有 `engine/runtime/renderscene/3dscene/pass/test_pass.cpp`，将其收敛为测试目标内部或实现文件局部的简单 pass，不新增公共 test-pass 类型、全局 pass registry 或第二套 Scene/View/Material 模型。
- [ ] 11.2 让简单 pass 复用同一个 SceneViewFamily/ViewInfo、`init_views()`、visibility、graphics context 和 ShaderMap Loader；删除裸 SPIR-V、手写 reflection、手写 binding layout 或 backend-specific slot 路径。
- [ ] 11.3 在 Forward Base Pass 前录制确定性 clear 或简单 indexed draw，使用同一帧可见输入和五组 logical bindings；测试记录必须能区分可见 Primitive 被消费、被剔除 Primitive 未产生 draw 和缺失 binding 被诊断。
- [ ] 11.4 保证测试 pass、Forward Base Pass、pending uploads 串行录入同一 graphics context/list，不隐藏 submit、不等待 GPU、不改变 runtime 正式 Renderer 职责。
- [ ] 11.5 中间验证只构建测试 pass 直接目标并运行一条确定性 command-recording smoke；真实 GPU/present、多帧、resize 和 failure injection 统一进入 Batch D。

## 12. Batch C4 — Bootstrap / Terminal / Shutdown

- [ ] 12.1 将 RHIDevice、RenderResourceManager、placeholder、RenderScene 和 primary viewport ownership 接入 Renderer internal domain，并把 owned primary viewport 接到 10.15 已闭合的 frame-owner policy；logical RT 完成创建后才能发布 Running 并开放普通 RenderCommand façade。
- [ ] 12.2 实现 placeholder/device-level bootstrap context 的 create→upload/transition→finish→explicit submit→wait specified completion，全有或全无发布且保留原始失败码。
- [ ] 12.3 实现 Renderer first-error latch 和 terminal 状态：停止新 frame/resource init、abort current recording、先清 Manager non-owning pointers，再 skip/dispose pending ownership payload。
- [ ] 12.4 实现正常 shutdown 的 producer stop→World destroy render states→Proxy/Material/Resource release FIFO→RenderCommandFence drain→Renderer final teardown task。
- [ ] 12.5 实现 RT teardown 的 abort/RenderScene clear/Manager clear/queue reclaim/viewport/placeholder/device 逆序销毁，确保 in-flight RHI payload 由 completion/deferred deletion 保活。
- [ ] 12.6 实现 DeviceLost 有限 teardown、waiter wake、RenderingThread return/join、Task Graph shutdown 和 Window 最后销毁；不得无限 wait/retry 或覆盖 first error。
- [ ] 12.7 中间验证只运行 Renderer bootstrap 成功、正常退出和一次 terminal disposal smoke，并构建 `Toy3dEditor`；完整初始化失败矩阵留到 Batch D。

## 13. Batch D — Concentrated Test and Independent Verification

- [ ] 13.1 建立集中 CPU View/visibility 测试：finite/infinite-far reversed-Z frustum、AABB inside/outside/intersect/touching、invalid matrix/near/view rect、multi-view independent visibility 和每帧结果重置。
- [ ] 13.2 建立 single-thread 完整场景流程：World→StaticMeshComponent→render state→Proxy/SceneInfo→resource upload→SceneViewFamily→init_views→visibility→MeshBatch→Material binding→test pass→Forward Base Pass→submit/present。
- [ ] 13.3 用完全相同场景和 Renderer/RHI 路径建立 multi-thread 流程，只切换 RenderingThreadMode，验证 same-producer FIFO、one-frame lag、Fence helping、SceneRenderer RT 析构、Proxy/Resource release 顺序和等价命令序列。
- [ ] 13.4 在 single/multi-thread E2E 中连续修改 transform、Camera、Material scalar/vector/texture 与 visibility，验证 frame N/N+1 数据边界、FIFO 最终值、不可见 Material 不物化和被剔除 Primitive 不产生 draw。
- [ ] 13.5 用一套 resource transaction 流程集中覆盖 frame abort、list discard、submit 明确失败、submit 边界未知、present Suboptimal/OutOfDate、candidate replacement failure 与 GPU completion 前旧 RHI object 保活。
- [ ] 13.6 用共享 failure-injection 流程覆盖 Engine/RenderingThread/Renderer/RHI bootstrap 各失败点、Material/VertexFactory validation、DeviceLost、pending ownership disposal 和 shutdown waiter wake；验证原始错误与逆序清理，不为每个失败点复制完整 fixture。
- [ ] 13.7 运行真实 Vulkan 多帧 smoke：validation layer 无错误、可见对象产生 indexed draw、被剔除对象不绘制、test pass 与 Base Pass 显式有序、resize/minimize/restore 可恢复、submit/present/completion 和正常退出闭环。
- [ ] 13.8 执行推荐 Windows Debug configure，构建 `Toy3dEditor` 和全部直接受影响测试 targets，运行 `ctest --test-dir build -C Debug --output-on-failure`，并记录实际命令、结果和未运行平台原因。
- [ ] 13.9 用 `rg` 核对废弃清单中的旧 transport/cache/ID/revision/command-list 原型、已替换的视图族与索引缓冲命名、全部 LOD 任务、第二套 Engine 抽象、公开 test-pass 类型和未登记新增类型无正式残留；运行 `openspec validate establish-game-render-framework --strict`。
- [ ] 13.10 将最终构建、测试、diff、Type Contracts 与三后端可实现性检查交给独立 sub-agent 使用 `verify-toy3d-build` 复核，主 agent 修复全部问题后再交付。
