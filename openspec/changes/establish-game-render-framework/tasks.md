## 1. Apply Governance

- [ ] 1.1 在每次 apply 开始时读取主 design、目标子 Spec 与直接前置完成状态，并在本次交付中记录选择依据。
- [ ] 1.2 在新增任何第一方具名类型前核对目标 Spec 的 Type Contracts；缺失时先更新 Spec 并以 `openspec validate establish-game-render-framework --strict` 验证。
- [ ] 1.3 每个 capability 完成后运行其单元测试、受影响目标构建和废弃符号搜索，只有三者通过才勾选该组完成任务。
- [ ] 1.4 每个新增或修改模块 MUST 标明 Game side、Render side 或 stateless bridge，并通过依赖审查验证 Game side 无 RenderScene/RHI、Render side 无 Game 对象回读。

## 2. Legacy Rendering Cleanup

- [ ] 2.1 生成旧 frame transport、Scene frame processor、resource cache/collector、typed ID/revision、空壳 command list 的定义/调用/测试/CMake 清单，并用 `rg` 验证覆盖全部正式入口。
- [ ] 2.2 删除旧 `RenderFramePacket/Queue/Dispatcher/Completion` 源码和 CMake/test target，并验证旧 transport 符号只剩 archive/OpenSpec 引用。
- [ ] 2.3 删除旧 Scene update batch、frame processor 和 Camera frame builder 正式路径，并验证 GameScene/RenderScene 不再生成旧 frame packet。
- [ ] 2.4 删除 `RenderResourceCache`、collector、render-resource typed ID/revision 与对应 cache/upload pipeline tests，并验证旧 identity 符号无运行时残留。
- [ ] 2.5 删除空壳 `RHIDeviceCommandList`、带该参数的 RenderCommand 原型及原型测试，验证公共 RHI 不再包含该类型。
- [ ] 2.6 调整 Engine 临时启动路径使删除后仍可配置和安全退出，不增加 no-op 成功 adapter，并构建 `Toy3dEditor` 验证阶段 0 build green。
- [ ] 2.7 运行保留的 Task Graph、RHI upload/state/viewport tests，验证清理未删除底层可复用能力。

## 3. Task Graph Runtime

- [ ] 3.1 复核 `task-graph-runtime` Type Contracts，确认本批除修改 `TaskGraphInterface` 外不新增类型，并 strict validate。
- [ ] 3.2 为 Task Graph 增加 composition-root-owned active publication 与 `is_running()`，验证启动前/运行中/关闭后三态测试。
- [ ] 3.3 实现受控 `TaskGraphInterface::get()`，验证未运行访问、正常访问和 shutdown race 诊断。
- [ ] 3.4 实现重复 active instance 启动拒绝和首次 instance 保持，验证两个 factory/start 竞争测试。
- [ ] 3.5 核对 GameThread/RenderingThread attach 与 same-producer FIFO，补充尚未 attach target 的拒绝测试。
- [ ] 3.6 核对显式 shutdown、tracked completion 和 waiter wake，运行完整 Task Graph test target。

## 4. RenderingThread Lifecycle

- [ ] 4.1 按 Type Contracts 新增 `RenderingThread` 与 `RenderingThreadMode`，验证头文件独立包含、所有权和线程注释。
- [ ] 4.2 实现 multi-thread OS thread 创建与 Task Graph RenderingThread attach，验证 thread id 和 named-thread TLS。
- [ ] 4.3 实现 ready handshake，使 attach/bootstrap result 发布前普通 façade 保持关闭，并验证提前 enqueue 被拒绝。
- [ ] 4.4 实现 named queue pump、wake 和 return request，不新增私有 queue，并验证 idle/wake/return 测试。
- [ ] 4.5 实现 single-thread controller，使 logical RT 映射 GT 且不创建 OS thread，并验证生命周期线程记录。
- [ ] 4.6 实现 start failure 的 stop/join cleanup，覆盖 thread create、attach 和 bootstrap callback 失败注入。
- [ ] 4.7 实现 teardown 后 request return/join 顺序，运行 repeated multi/single start-stop 测试。

## 5. RenderCommand Transport

- [ ] 5.1 按 Type Contracts 新增内部 `RenderCommandTask<Callable>`，验证 move-only callable、`void() noexcept` 编译期约束。
- [ ] 5.2 实现 GT multi-thread enqueue 到 RenderingThread named queue，验证命令名、FIFO 和 FireAndForget 行为。
- [ ] 5.3 实现 logical RT inline 路径，验证嵌套 RenderCommand 不重复排队且保持顺序。
- [ ] 5.4 实现 GT single-thread inline 路径，验证与 multi-thread 使用相同 callable body。
- [ ] 5.5 实现 AnyWorker producer fail-fast 和未启动/关闭后 enqueue 诊断，覆盖所有非法 producer 状态。
- [ ] 5.6 建立内部 façade enable/disable 生命周期，不暴露 Binding/Route/Registry/Renderer getter，并用公共头文件审查验证。
- [ ] 5.7 运行 move-only ownership、terminal disposal thread 和高数量 FIFO 压力测试。

## 6. Frame Synchronization

- [ ] 6.1 按 Type Contracts 新增 `RenderCommandFence`、`FrameEndSync`、`RenderFenceWaitResult`，验证无 GPU fence/RHI ownership。
- [ ] 6.2 实现 tracked Fence task 和 GT wait/helping，验证 Fence 前序 callable 全部完成。
- [ ] 6.3 验证 Fence 完成不等待 GPU completion，使用可控 fake queue 分离两种信号。
- [ ] 6.4 实现双 Fence one-frame lag 和 zero-lag 配置，验证连续帧最大领先量。
- [ ] 6.5 实现 explicit flush rendering commands，并验证普通 setter/init 不隐式调用 flush。
- [ ] 6.6 实现 framework failure/renderer terminal 的 waiter wake 与只读结果传播，覆盖等待竞态测试。

## 7. Renderer and Scene Ownership

- [ ] 7.1 按 Type Contracts 新增最小 `Renderer` shell、`SceneInterface`、`RenderScene`，验证模块依赖方向和头文件无 backend 类型。
- [ ] 7.2 实现 Renderer shell GT 创建、logical RT 初始化/teardown和稳定地址，验证 GT 不读取内部可变状态。
- [ ] 7.3 实现 Renderer-owned RenderScene 创建销毁，验证 RT-only mutation thread assertions。
- [ ] 7.4 实现 World non-owning SceneInterface bind/unbind，验证 World 不包含或拥有 RenderScene。
- [ ] 7.5 定义 SceneInterface 最小生命周期操作并验证不暴露 RT query、RHI 或 Renderer getter。
- [ ] 7.6 覆盖 World 先 remove render state、RenderScene 后 teardown 的顺序测试。

## 8. Engine Composition Root

- [ ] 8.1 直接修改 `engine/runtime/engine.h/.cpp::toy3d::Engine`，按 Type Contracts 新增 `EngineLifecycleState` 并收敛成员声明，验证不存在第二个 Engine composition-root 类型。
- [ ] 8.2 在现有 `toy3d::Engine` 中将 Platform/Window/RHISurface 创建与 Task Graph start 按顺序接入，覆盖每个失败点的逆序清理测试。
- [ ] 8.3 让现有 `toy3d::Engine` 创建 Renderer shell 和 RenderingThread controller，并通过 ready result 决定是否进入 Running。
- [ ] 8.4 将 FrameEndSync 接入现有 `toy3d::Engine::main_loop()`，验证主循环不直接等待 GPU 或操作 RT internals。
- [ ] 8.5 从 `toy3d::Engine` 移除 RHIDevice、RHIViewportContext、RenderResourceManager 和具体 SceneRendering 的直接 ownership/调用，并用 `rg` 验证。
- [ ] 8.6 实现现有 `toy3d::Engine::exit()` 的 producer stop、Renderer lifecycle request、join、Task Graph shutdown 和 Window 销毁顺序，运行 Engine lifecycle 测试。
- [ ] 8.7 用 `rg` 和类型清单验证未新增 `EngineDomain`、`EngineContext`、`EngineServices`、`RuntimeEngine`、`GameEngine` 或等价 Engine wrapper/service locator。

## 9. Primitive Proxy Lifecycle

- [ ] 9.1 按 Type Contracts 新增 `PrimitiveSceneProxy`、`PrimitiveSceneInfo`，验证多态析构和 RT ownership 注释。
- [ ] 9.2 实现 Component 创建 Proxy 并通过 Add command 转移 unique ownership，验证 GT 投递后不再拥有。
- [ ] 9.3 实现 RenderScene 创建 SceneInfo、注册 Proxy 和返回 opaque identity，验证 GT 不能通过接口解引用。
- [ ] 9.4 实现 transform/visibility/material slots 的 owned-value FIFO update，验证 Draw 前最终值。
- [ ] 9.5 实现 remove 先摘除全部索引再 RT 析构，覆盖重复 remove、失序 update 和销毁线程测试。
- [ ] 9.6 覆盖 Proxy remove/update 先于 Mesh/Material/Texture release 的生命周期排序测试。

## 10. View and Render Flow

- [ ] 10.1 按 Type Contracts 新增 `SceneView`、`ViewFamily`、`SceneRenderer`，并用 `rg` 验证没有新增 `Snapshot` 命名类型。
- [ ] 10.2 实现 GT 从 Camera/Window values 构造 SceneView，验证投递后 Camera 改动不影响既有 frame。
- [ ] 10.3 实现 ViewFamily 聚合 SceneInterface、views、output/config values，验证不读取 RT RenderScene。
- [ ] 10.4 实现 SceneRenderer factory 和 Draw command ownership transfer，验证正常执行与 terminal skip 都在 logical RT 析构。
- [ ] 10.5 实现 begin-frame、单 graphics context/list 和显式 pass 顺序骨架，验证 multi/single 输出命令序列一致。
- [ ] 10.6 实现 NotReady/OutOfDate/Suboptimal frame policy，验证最小化不录制 Draw且 pending resource 保留。

## 11. RHI Frame Submission

- [ ] 11.1 按 Type Contracts 新增 `RHIFrameEndResult`，迁移公共 RHI 与调用方并验证无 backend token 泄漏。
- [ ] 11.2 修改 viewport end-frame 外层结果表示 business submit，验证 submit success/present success 矩阵。
- [ ] 11.3 实现 submit success + present Suboptimal/OutOfDate 的 commit 语义，验证 completion 有效且标记 rebuild。
- [ ] 11.4 实现 submit failure 返回外层失败且不提供业务 completion，验证 resource/state transaction discard。
- [ ] 11.5 前移所有可失败 validation，保证 native submit 后 publication 不返回失败，覆盖内部不变量 terminal 测试。
- [ ] 11.6 修正 abort 最小 submit 不发布业务状态，覆盖 abort success/failure 和同步对象不复用测试。
- [ ] 11.7 迁移 Vulkan backend，并以接口/测试 double 评估 D3D11、D3D12、VulkanPortable 可实现性与 Unsupported 路径。

## 12. RHI Resource State and Completion

- [ ] 12.1 复核 Type Contracts；实现需要具名 state summary/submission record 时先登记，否则沿用现有类型并 strict validate。
- [ ] 12.2 核对 transition 只修改 command-list local tracker，补充 discard 不污染 committed state 测试。
- [ ] 12.3 修正 queue 按实际 submit 顺序 validation/commit，覆盖录制顺序与提交顺序不同的测试。
- [ ] 12.4 为所有 RHI object 增加不可变 device ownership identity，覆盖 cross-device record/submit 拒绝测试。
- [ ] 12.5 核对 command list 强引用 resource/view/binding/pipeline/staging 到 completion，覆盖提前释放上层 refs 测试。
- [ ] 12.6 核对 completion-driven allocator/descriptor/staging/deferred deletion 回收，覆盖 completion 前后边界。
- [ ] 12.7 为 D3D11 completion 记录 event-query 实现 contract/test seam，并验证 CPU Execute 返回不等于 GPU completion。

## 13. RenderResource Manager

- [ ] 13.1 按 Type Contracts 新增 `RenderResource`、`RenderResourceState`、`RenderResourceManager`，验证不新增公开 Prepared/Transaction/Registry 类型。
- [ ] 13.2 实现 RT-only init/release 和状态转换 assertions，覆盖错误线程、重复 init/release 测试。
- [ ] 13.3 实现 non-owning pending collection 与 release removal，覆盖 pointer 不残留测试。
- [ ] 13.4 实现 `record_pending_uploads`，验证 upload 返回前 staging 已复制且 resource 自身不 submit/wait。
- [ ] 13.5 实现 business submit success commit，验证 Ready 发布和 initial payload 释放。
- [ ] 13.6 实现 abort/submit failure discard，验证 PendingUpload 与 initial payload 重试。
- [ ] 13.7 实现 ownership-transfer release 与 in-flight RHI refs，验证不 wait GPU 的安全析构。
- [ ] 13.8 实现 `enter_terminal` 先 discard/clear pointers 后停止解引用，覆盖 skipped owner payload 回归测试。

## 14. StaticMesh Resources

- [ ] 14.1 按 Type Contracts 新增 StaticMeshRenderData、各 buffer resource 与 VertexFactory，验证没有额外 LOD/handle wrapper。
- [ ] 14.2 实现 position/attribute/color/index initial payload 和独立 init，覆盖 descriptor与非法数据 validation。
- [ ] 14.3 实现 VertexFactory 对 buffer layout/binding 的 RT 初始化，验证不拥有 Asset或继承 RenderResource。
- [ ] 14.4 实现全部必要子资源的整体 ready gate，覆盖任一 buffer failure 的 fallback/skip。
- [ ] 14.5 实现第一阶段全部 LOD 一次初始化，验证无 streaming/partial residency 分支。
- [ ] 14.6 实现完整 candidate RenderData replacement 和旧对象 ownership-transfer release，覆盖 candidate failure 保留 active。

## 15. Texture Resources

- [ ] 15.1 按 Type Contracts 新增 `TextureRenderResource`，验证不新增 TextureReference/ID/candidate wrapper/revision。
- [ ] 15.2 实现 Texture Asset ownership 与 MaterialInstance 强引用覆盖，验证 proxy non-owning pointer 生命周期。
- [ ] 15.3 实现 initial texture upload 和 Ready 发布，覆盖 row/slice pitch validation 与 submit retry。
- [ ] 15.4 实现同 native texture 内容更新，验证 view identity/binding generation 不变。
- [ ] 15.5 实现 descriptor/format/mip candidate 创建、upload 和 submit success publication。
- [ ] 15.6 实现 candidate abort/failure 保留 active，并覆盖 retry/discard 测试。
- [ ] 15.7 实现 binding generation 失效与旧 view command-list 保活，覆盖 Draw 后 replacement 到 completion。

## 16. Material Updates

- [ ] 16.1 按 Type Contracts 修改 MaterialInstance 并新增 MaterialRenderProxy，验证 Proxy 不继承 RenderResource且无普通 revision/ID。
- [ ] 16.2 实现 scalar/vector setter 捕获 stable proxy identity、parameter identity 和 owned value，覆盖类型校验与 FIFO。
- [ ] 16.3 实现 texture setter 的新 Asset 强引用先建立、proxy update、旧引用后释放顺序测试。
- [ ] 16.4 实现 RT parameter table 与 constants/binding dirty 标记，验证 setter 不创建 RHI buffer或 flush。
- [ ] 16.5 实现可见 Draw 前 override/default resolve 和 frame-local constants/binding 按需物化，覆盖多 setter 合并。
- [ ] 16.6 实现不可见 Material 不物化路径，验证仅保持 dirty state。
- [ ] 16.7 建立 static switch/permutation/layout/render-state candidate replacement seam，覆盖普通 setter 拒绝结构性变化。

## 17. Renderer Bootstrap

- [ ] 17.1 复核本 capability 不新增类型，将 RHIDevice/Manager/placeholder/viewport ownership 接入 Renderer internal domain。
- [ ] 17.2 实现 logical RT 创建 RHIDevice 与 RenderResourceManager，覆盖 device initialize failure 回滚。
- [ ] 17.3 实现 placeholder empty resource、device context upload/transition、finish 和 explicit submit。
- [ ] 17.4 实现等待指定 bootstrap completion 和全有或全无 publication，覆盖 record/submit/wait failure。
- [ ] 17.5 实现 primary viewport 创建与 Running publication，验证 façade 在此前保持关闭。
- [ ] 17.6 实现 viewport failure 的 placeholder/manager/device 逆序清理且保留原始错误。
- [ ] 17.7 验证 Running 后普通资源只走 frame-local pending upload，不复用 bootstrap wait。

## 18. Renderer Terminal and Shutdown

- [ ] 18.1 按 Type Contracts 新增 `RendererLifecycleState`、`RenderingStatus`，验证原子 state 与 immutable first-error publication。
- [ ] 18.2 实现 first-error latch 和 secondary diagnostic，覆盖 DeviceLost 后 cleanup failure 不覆盖。
- [ ] 18.3 实现 terminal 停止新 frame/resource init、abort recording 和 ResourceManager pointer clear 顺序。
- [ ] 18.4 实现 pending callable FIFO skip 与 logical RT payload disposal，覆盖 Draw/Proxy/Resource ownership。
- [ ] 18.5 实现 pending Fence completion和 GT waiter terminal wake，覆盖 terminal 竞态。
- [ ] 18.6 实现正常 shutdown 的 producer stop、World remove、resource release 和内部 final teardown task。
- [ ] 18.7 实现 RT abort/Scene clear/manager clear/queue reclaim/viewport-placeholder-device teardown 顺序。
- [ ] 18.8 实现 DeviceLost 有限 teardown，不无限 wait/retry，覆盖 wait-idle failure 和 Engine exit 不挂死。
- [ ] 18.9 实现 teardown 后 façade clear、return request、RenderingThread join 与 Task Graph shutdown 验证。

## 19. Integration and Independent Verification

- [ ] 19.1 建立最小 World+Primitive+Mesh+Texture+Material+Camera 场景，验证更新→Draw→submit→present 的端到端顺序。
- [ ] 19.2 在 multi-thread 模式连续运行多帧，验证 FIFO、one-frame lag、resize/minimize/restore 和无 validation error。
- [ ] 19.3 在 single-thread 模式运行同一场景，验证同一 callable/Renderer/RHI 路径和等价结果。
- [ ] 19.4 对 Engine/RenderingThread/Renderer/RHI bootstrap 每个初始化阶段执行 failure injection，验证原始错误与逆序清理。
- [ ] 19.5 对 submit、present、abort、DeviceLost 和 pending ownership 执行 failure matrix，验证无悬空 pointer、提前 native deletion 或 waiter hang。
- [ ] 19.6 用 `rg` 验证旧 transport/cache/ID/revision/command-list 原型、第二套 Engine 抽象和未登记新增类型无正式残留，并 strict validate OpenSpec。
- [ ] 19.7 配置 Windows Debug build，构建 `Toy3dEditor` 与所有受影响测试 target，运行 `ctest --output-on-failure`。
- [ ] 19.8 将最终构建、测试、diff 和 Type Contracts 一致性检查交给独立 sub-agent 使用 `verify-toy3d-build` 复核，修复全部问题后交付。
