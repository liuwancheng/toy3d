# Core 公共模块使用索引

本文供开发者和 AI 快速定位 `engine/core/` 的共享基础设施。这里只记录入口和常用调用方式；接口、测试与专项设计文档仍是最终依据。

## 快速索引

| 能力 | CMake target | 公共入口 | 可执行示例 |
|---|---|---|---|
| 文件系统 | `Toy3dFileSystem` | `file_system/file_system.h`、`native_platform_file.h` | `engine/core/tests/file_system_tests.cpp` |
| 日志 | `Toy3dLogging` | `logging/logger.h` | `engine/core/logging/logger.cpp` |
| 数学 | `Toy3dMath` | `math/math.h`、`math/random.h` | 现有 runtime/renderscene 调用方 |
| 线程、事件、Queue | `Toy3dThreading` | `threading/thread.h`、`event.h`、`runnable_thread.h`、`containers/queue.h` | `engine/core/tests/threading_tests.cpp`、`queue_tests.cpp` |
| Task Graph | `Toy3dTaskGraph` | `task_graph/task_graph.h`、`graph_task.h` | `engine/core/tests/task_graph_tests.cpp`、`task_graph_scheduler_tests.cpp` |

调用目标按需链接：

```cmake
target_link_libraries(MyTarget
    PRIVATE
        Toy3dFileSystem
        Toy3dTaskGraph)
```

`Toy3dTaskGraph` 会传递 `Toy3dThreading`。不要直接包含 `stalling_task_queue.h`、`bounded_mpmc_queue.h` 或继承 `BaseGraphTask`；这些是实现层入口。

## FileSystem

业务模块优先接收 composition root 注入的 `FileSystem&`，使用 `VirtualPath`；只有平台实现和 composition root 才直接使用 `NativePlatformFile`、`PhysicalPath` 和 mount 配置。所有 `FileResult<T>`、`FileStatus` 都必须检查。

```cpp
const auto parsed = toy3d::VirtualPath::parse("/shader/example.bin");
if (!parsed.succeeded())
{
    return;
}

const auto content = file_system.read_binary(parsed.value());
if (!content.succeeded())
{
    // 记录 content.status() 后返回。
    return;
}
```

mount 应在启动阶段完成并调用 `FileSystem::freeze()`；领域层不要绕过虚拟文件系统拼接宿主路径。完整边界见 `document/core-infrastructure-design.md`。

## Logging 与 Math

`Logger` 由 composition root 初始化和退出，普通调用方使用宏保留源码位置：

```cpp
TOY_LOG_INFO("Loaded {} entries", entry_count);
TOY_LOG_ERROR("Load failed: {}", error_message);
```

数学类型统一从 `math/math.h` 获取，例如 `toy3d::vec3`、`toy3d::mat4x4`、`toy3d::quat`、`toy3d::Degree`。渲染坐标与矩阵约定以 `AGENTS.md` 和 RHI 设计为准，不在业务模块建立另一套类型别名。

## Threading

- 短生命周期函数线程使用 `Thread(ThreadManager&, name, function)`，销毁前显式 `join()`。
- 需要 `init/run/stop/exit` 生命周期时实现 `Runnable`，通过 `RunnableThread::create()` 创建，并检查 `RunnableThreadCreateResult`。
- `Event` 支持 `AutoReset` 和 `ManualReset`；跨线程停止通常由 owner 调用 `trigger()` 或 `RunnableThread::request_stop()`。
- `Queue<T, QueueMode::Spsc>` 和 `Queue<T, QueueMode::Mpsc>` 都严格只有一个 consumer；`enqueue()` 失败必须处理，`dequeue()` 返回 `false` 表示当前为空。

```cpp
toy3d::ThreadManager thread_manager;
toy3d::Event finished(toy3d::EventMode::ManualReset);
toy3d::Thread worker(thread_manager, "Prepare", [&finished]()
{
    // CPU work
    finished.trigger();
});
finished.wait();
worker.join();
```

## Task Graph

`ThreadManager` 和 `TaskGraphInterface` 由 composition root 持有。创建后，外部 owner thread 必须先 attach 自己的 Named Thread；多线程模式下 `RenderingThread` 由 Render Thread attach，单线程模式下逻辑渲染线程映射为 `GameThread`。

```cpp
toy3d::ThreadManager thread_manager;
toy3d::TaskGraphConfig config;
config.multithreaded = false; // 最小示例由 GameThread 顺序处理全部目标。
auto created = toy3d::create_task_graph(config, thread_manager);
if (!created.succeeded())
{
    return;
}

std::unique_ptr<toy3d::TaskGraphInterface> task_graph = created.take_task_graph();
if (!task_graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded())
{
    return;
}

toy3d::GraphEventRef prepared = toy3d::dispatch_graph_task(
    *task_graph,
    "PrepareData",
    [](toy3d::NamedThread, const toy3d::GraphEventRef&)
    {
        // 只处理线程安全、生命周期明确的 CPU 数据。
    });

toy3d::GraphEventArray prerequisites{prepared};
toy3d::GraphEventRef consumed = toy3d::dispatch_graph_task(
    *task_graph,
    "ConsumeData",
    [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
    task_graph->get_render_thread(),
    &prerequisites);

const toy3d::TaskWaitResult waited = task_graph->wait_until_task_completes(
    consumed, toy3d::NamedThread::GameThread);
if (!waited.succeeded())
{
    return;
}

const toy3d::TaskGraphShutdownResult stopped = task_graph->shutdown(
    toy3d::TaskGraphShutdownMode::Drain);
if (!stopped.succeeded())
{
    return;
}
```

关键约束：

- Task 输入优先是 immutable value 或生命周期覆盖完成事件的共享所有权；不要捕获即将离开作用域的引用。
- `NamedThread::AnyWorker` 用于普通 CPU 任务；必须在指定线程执行的任务才投递到 `GameThread` 或 `RenderingThread`。
- prerequisite 用 `GraphEventArray` 表达；需要延迟当前事件完成时才调用 `completion_event->dont_complete_until()`。
- Named Thread owner 等待时会 helping；禁止任务等待自身或形成依赖环。
- 正常退出使用 `Drain`，需要快速终止时使用 `CancelPending`，并检查 shutdown result。
- 更完整的线程模型、错误语义和迁移顺序见 `document/threading-task-graph-design.md`。

## 新增公共能力前

先搜索本页、`engine/core/`、相关测试和专项设计。若能力属于跨 runtime/editor/tools 的通用文件、日志、线程、任务、时间、缓存等系统，应扩展独立 Core contract，不得在业务模块复制一套实现。
