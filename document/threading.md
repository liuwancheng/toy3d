# Threading：线程、队列与 TaskGraph

## 定位

Toy3dCore 提供 engine/core/threading 的 Thread/Event/RunnableThread/ThreadManager/容器，以及其 task_graph 子目录的 TaskGraph/GraphTask/GraphEvent/NamedThread；两者共用一个库。Game/Render 策略在 [Render Framework](render-framework.md)。

完整调用见 engine/core/tests/threading_tests.cpp、queue_tests.cpp、task_graph_tests.cpp、task_graph_scheduler_tests.cpp。

## 线程与 Queue

- 启动握手区分线程创建与 runnable 初始化成功；init/run/exit 生命周期由 RunnableThread/ThreadManager 管理，失败不发布半初始化状态。
- stop 是协作请求；唤醒阻塞、退出、显式 join。不能强制终止后当成功，也不能依赖一个永不响应 runnable 自动退出。
- Event 自动/手动复位语义明确；唤醒不是业务状态，检查 predicate、防漏唤醒。
- SPSC 一个生产者/消费者，MPSC 多生产者/一个消费者；明确消费线程、容量、payload ownership、enqueue 失败处理，不能静默丢任务。
- scheduler 的 bounded MPMC/stalling queue 是私有实现，业务不直接使用或包装为第二套任务系统。

## TaskGraph contract

composition root 创建/持有/关闭一个 active TaskGraph。TaskGraphInterface::get()/is_running() 只暴露运行生命周期内的 scheduler，不延长 root 生命周期，不是业务服务定位器。

NamedThread 正确 attach，区分 GT/RT/worker/Unknown；SingleThread 逻辑 RT 映射 GT，不授权其它线程执行 RT 业务。GraphTask 指定 desired thread/priority/subsequents mode，GraphEvent 表达 prerequisite；dispatch 后 payload 所有权交调度器，执行/放弃各路径恰好销毁一次。

- TrackSubsequents 产生可等待 completion；FireAndForget 仅适合不需要结果且 ownership 完整转移。
- GraphEvent 是 CPU 完成点，不代表 GPU/WSI/外部 I/O。
- 同线程等待使用 scheduler 已知线程处理入口，不能消费者等自己后面的任务；Unknown 等待不授予 NamedThread 权限。多线程下 GT 等待/Drain 会帮助执行 AnyWorker 就绪任务，所以 AnyWorker 不保证物理 worker 执行；普通后台任务不得假定该路由隔离 GT。
- 不持业务锁等待任务；长期阻塞进程/I/O 用专用线程，不占 worker。
- 按现有 TaskGraphStatus/Exception 处理失败，不虚构 enqueue bool/全局 pool API。

## 资源加载线程

`engine/runtime/asset_loader` 的 AssetLoader 是资产对解码的统一入口，拥有一个由 composition root 注入的专用线程（`ThreadManager` 上的 "AssetLoader"）与四级优先级队列（Critical/High/Normal/Low，同级 FIFO）；它复用 Core 的 Thread/Event，不新建第二套任务系统，也不注册全局单例。队列按请求累积，但同一 identity 只保留一个在途任务（single-flight），逐帧轮询不会堆积解码。需要"立即拿到 TextureRef"的组件（如 `MaterialLibrary`）不再自己解码，而由 composition root 注入 resolver，策略见第 3 段。

加载线程只读 `FileSystem`/`AssetIndex` 快照（GT 独占 mount/refresh/发布），只产出 owned CPU payload，不访问 GPU/World/UI，也不创建运行时对象；每种资源的知识都在自己的 `AssetLoadJob` 里（`decode` 跑加载线程，`adopt`/`bytes` 跑 GT），门面只调度接口。GT 在 `tick()` 的 adopt 阶段接管并创建/退役运行时对象。等待者由在途任务以 shared_ptr 持有，因此调用方丢弃句柄不会取消共享解码，只有全部等待者显式 `cancel()` 时任务才在开始前被丢弃。

装配路径（Scene 装配的环境与 StaticMesh、PIE、World Settings、Game 启动 Scene、PIE/Game 与材质窗口的材质贴图解析）无法在缺资产时继续，因此允许对 `AssetLoadPriority::Critical` 请求做**有界等待**：`load_assembly_texture`/`load_assembly_static_mesh` 内部用 `AssetLoader::wait(handle, timeout)`，等待期间仍由 GT 执行 adopt，超时/失败/在途失效都返回空并给出诊断；它不持业务锁。装配等待可由帧循环内的动作触发（打开 Scene、启动 Scene 的自动装载、启动 PIE、改 World Settings），上界是超时值而非"一帧内完成"，因此这些动作不是无成本的。窗口与预览的 Environment cube 不在帧内等待：逐帧 `tick()` 轮询并保留旧图。

`adopt()` 自己也可能触发装配等待（StaticMesh 的 adopt 解析材质，材质解析再解析贴图），所以 `tick()` 必须可重入：它每次只从共享结果队列取一个条目再 adopt，嵌套的 `wait`/`tick` 因此能继续消费同一批结果，而不会等一个已经解码完、只是尚未 adopt 的资产直到超时。加载线程为串行且不抢占在途解码，一次装配等待的上界是"当前那次解码结束"与超时值中的较小者，命中共享缓存时立即返回。`decode` 抛出的异常被记为该条目的失败诊断，加载线程不会因此退出。

退出顺序为禁新请求 → 清队列与在途 → join 加载线程 → 再 drain 渲染；长期 I/O 解码因此不占 AnyWorker 配额。每个进程只有一个加载器实例：Engine 在 `initialize_render_framework` 创建它、在 `shutdown_render_framework` 里（世界释放之后、线程管理器销毁之前）join，并通过 `Application::set_asset_loader` 在 `on_initialize` 之前注入给应用——Editor 与 Game host 用同一份，Editor 不再自建；测试宿主仍可各自构造自己的加载器。加载位置的生命周期见 [Assets](assets.md#资产加载门面)。

## Shutdown 与验证

先停止生产，再按 shutdown mode 处理已接受任务与依赖；渲染 drain 后才关闭 TaskGraph。Event/root/payload 必须覆盖消费者生命周期。

改动覆盖依赖未完成、dispatch 失败、worker 异常、同线程等待、重复关闭、停止中 enqueue、队列满、析构线程及 SingleThread。构建相关 core target/测试；渲染调用补验证 RenderCommand/Fence，不仅测 scheduler。
