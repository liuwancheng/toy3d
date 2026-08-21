# Toy3d 线程与 Task Graph 设计

## 1. 文档状态与核心决定

本文定义 Toy3d 第一版共享线程基础设施与 Task Graph。设计依据以本机 `D:/ue4.27plus/Engine` 中的 UE4.27Plus 源码为主，不以 Toy3d 当前任何多线程、渲染线程或局部队列实现为兼容前提。

公共术语和接口形状采用 UE 容易识别的命名，但遵守 Toy3d 现有规范：

- 不使用 `F`、`T`、`I`、`E` 类型前缀；
- 类型使用 PascalCase，函数使用 snake_case；
- `FRunnable` 对应 `Runnable`；
- `FRunnableThread` 对应 `RunnableThread`；
- `FThread` 对应轻量 `Thread`；
- `FEvent/FEventRef` 对应 `Event/EventRef`；
- `ENamedThreads::Type` 对应 `NamedThread`；
- `FGraphEvent/FGraphEventRef` 对应 `GraphEvent/GraphEventRef`；
- `FTaskGraphInterface` 对应 `TaskGraphInterface`；
- `TGraphTask<TaskType>` 对应 `GraphTask<TaskType>`；
- `ESubsequentsMode` 对应 `SubsequentsMode`；
- `TQueue<T, EQueueMode>` 对应 `Queue<T, QueueMode>`；
- `FStallingTaskQueue` 对应内部 `StallingTaskQueue`。

第一版采用以下结论：

- 长驻专用线程与短 Task 是不同抽象；Render Thread 使用 `RunnableThread`，细粒度并行使用 Task Graph；
- `Runnable` 使用 UE 的 `init() -> run() -> exit()` 生命周期，`stop()` 表达协作停止；
- `RunnableThread::create()` 等待目标线程完成 `init()`，与 UE 的 thread-init handshake 一致；
- Task Graph 支持 prerequisite、subsequent、completion `GraphEvent`、Named Thread 路由以及等待时 pump/help；
- Game Thread、Rendering Thread 和 Any Worker 是第一版完整执行域；
- Task Graph 由 composition root 创建并注入，不提供 UE 风格全局 `Get()` singleton；
- 无锁队列限定为明确并发模型；空闲 worker 通过独立 Event/condition variable 休眠；
- 第一版不实现 fiber、coroutine、work stealing、Future、单 Task 取消、强杀线程、动态 worker 数量或平台原生 priority/affinity。

## 2. 用例与非目标

### 2.1 用例

基础设施必须支持：

1. 创建有名称、显式生命周期和显式完成等待的长驻线程；
2. 在目标线程完成初始化后，创建侧才收到成功结果；
3. 将短 Task 定向到 Game Thread、Rendering Thread 或 Any Worker；
4. 使用 `GraphEventRef` 表达 Task completion 和 prerequisite；
5. 使用与 UE 相近的 `GraphTask<TaskType>::create_task().construct_and_dispatch_when_ready()` 写法；
6. 在一个 Task 中使用 `dont_complete_until()` 延迟自身 completion；
7. Named Thread 主动 pump 自己的 Task queue；
8. worker 或 Named Thread 等待 completion 时帮助调度系统取得进展；
9. 在多线程关闭时把 Any Worker 和 Render logical target 确定性地映射到 Game Thread；
10. 对线程创建、Task dispatch、过载、异常和 shutdown 给出诊断；
11. 为未来 Render Thread、并行 prepare、Pass recording preparation 和共享异步基础设施提供稳定边界。

### 2.2 非目标

第一版不做：

- 不复制 UE 的宏、stats、memory stack、TLS allocator、forkable thread 或历史兼容层；
- 不复制 UE 的全局 `FTaskGraphInterface::Get()`；
- 不建立三套 normal/high/background worker thread set；
- 不提供 `suspend()`、`resume()`、强制 `kill()` 或 `detach()`；
- 不提供隐式 `std::async`；
- 不实现 Task 返回值 Future、continuation 语法糖或 coroutine；
- 不支持 dispatch 后再任意追加 prerequisite；
- 不支持单个已 dispatch Task 的撤回或抢占式取消；
- 不把 Render Thread 做成 worker pool 中可漂移的 Task；
- 不把 Render Pass 依赖、RHI resource state、frame lag 或 frame drop 策略下沉到 Task Graph；
- 不允许 blocking file/network I/O 长期占用 Task Graph worker；
- 不承诺实时性、严格公平性或 OS 调度优先级；
- 不把所有同步结构都实现为 lock-free。

## 3. UE4.27Plus 源码分析

### 3.1 核对范围

本设计核对了以下本地源码：

- `Engine/Source/Runtime/Core/Public/HAL/Runnable.h`
- `Engine/Source/Runtime/Core/Public/HAL/RunnableThread.h`
- `Engine/Source/Runtime/Core/Public/HAL/Thread.h`
- `Engine/Source/Runtime/Core/Private/HAL/Thread.cpp`
- `Engine/Source/Runtime/Core/Private/Windows/WindowsRunnableThread.cpp`
- `Engine/Source/Runtime/Core/Private/HAL/PThreadRunnableThread.cpp`
- `Engine/Source/Runtime/Core/Public/HAL/Event.h`
- `Engine/Source/Runtime/Core/Public/Misc/IQueuedWork.h`
- `Engine/Source/Runtime/Core/Public/Misc/QueuedThreadPool.h`
- `Engine/Source/Runtime/Core/Private/HAL/ThreadingBase.cpp`
- `Engine/Source/Runtime/Core/Public/Async/AsyncWork.h`
- `Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h`
- `Engine/Source/Runtime/Core/Private/Async/TaskGraph.cpp`
- `Engine/Source/Runtime/Core/Public/Containers/Queue.h`
- `Engine/Source/Runtime/Core/Public/Containers/LockFreeList.h`

### 3.2 UE 的线程层

UE 将长驻线程与 Task 分开：

- `FRunnable` 定义 `Init()`、`Run()`、`Stop()`、`Exit()`；
- `Init()`、`Run()`、`Exit()` 在新线程上下文执行；
- `Stop()` 从请求停止的一侧调用，只能做线程安全的置位和唤醒；
- `FRunnableThread` 保存 name、thread id、runnable、TLS 和 thread-init event；
- 创建侧等待 `Init()` 完成，初始化失败不会伪装成线程启动成功；
- `WaitForCompletion()` 是明确的 join；
- `FThread` 是函数式轻量包装，并提示普通并行工作优先使用 Task Graph；
- UE 源码中的 `FThread::Detach()` 被禁用，析构前必须 join。

Toy3d 保留这些生命周期语义，但第一版用 `std::thread` 实现，不暴露标准库无法可移植兑现的 stack size、priority 和 affinity。

### 3.3 UE 的普通线程池层

`FQueuedThreadPool` 与 Task Graph 是两套设施：

- `IQueuedWork` 只有执行与放弃，没有 prerequisite；
- pool 使用锁保护待执行 work 与空闲 thread 列表；
- worker 睡在 event 上，有 work 时被唤醒；
- `FAsyncTask` 在其上补 completion、未开始 work 撤回和同步 fallback。

Toy3d 第一版不再建立一套公共 `QueuedThreadPool`。短 CPU work 统一进入 Task Graph；未来 blocking executor 必须根据文件、进程或网络的真实用例独立设计，不能占满 compute worker。

### 3.4 UE Task Graph 的核心

UE4.27 Task Graph 的长期价值不在模板外观，而在以下 contract：

- `FGraphEvent` 是 completion，同时保存等待它的 subsequent；
- Task 保存未完成 prerequisite 数；计数降到零时才进入 ready queue；
- `TGraphTask<TaskType>` 把用户 Task 与调度状态组合，并提供 create/construct/dispatch 流程；
- `ESubsequentsMode::TrackSubsequents` 产生 completion event；`FireAndForget` 省略它；
- Task 可以路由到 Named Thread 或 Any Thread worker；
- Game/Render 等 Named Thread 由外部线程拥有，并 attach 到 Task Graph；
- Named Thread 主动运行自己的 queue；
- Named Thread 等待 completion 时继续处理 Task；未知外部线程才阻塞在 event 上；
- `DontCompleteUntil()` 允许正在执行的 Task 把自身 completion 延迟到子 Task 完成；
- worker ready queue 将 lock-free FIFO、priority、stalled worker 状态和 event wakeup 组合起来。

Toy3d 保留 prerequisite/completion、Named Thread、wait-time helping 和 `dont_complete_until()`，裁掉多 priority thread set、全局 singleton、复杂 allocator 和 stats 宏。

### 3.5 UE 无锁容器的启示

UE `TQueue` 只支持 SPSC 或 MPSC，并明确只有一个 consumer。更复杂的 `LockFreeList.h` 使用 indexed link、ABA counter、一次分配 link allocator、closable list 与 cache-line padding。

结论是：无锁容器必须把 producer/consumer 数量、容量、分配行为和回收约束写入 contract。Toy3d 不提供一个含糊的通用 lock-free MPMC linked queue。

## 4. 模块、目录与 CMake target

基础设施拆为两个独立目标：

```text
engine/core/threading/
├── runnable.h
├── runnable_thread.h/.cpp
├── thread.h/.cpp
├── thread_manager.h/.cpp
├── event.h/.cpp
└── containers/
    ├── queue.h
    └── bounded_mpmc_queue.h        internal building block

engine/core/task_graph/
├── named_thread.h
├── graph_event.h/.cpp
├── graph_task.h
├── task_graph_interface.h
├── task_graph.h/.cpp               private concrete implementation
├── task_graph_types.h
└── stalling_task_queue.h/.cpp      private scheduler queue

engine/core/tests/
├── threading_tests.cpp
├── queue_tests.cpp
└── task_graph_tests.cpp
```

目标关系：

```text
Threads::Threads
      ↑ PRIVATE
Toy3dThreading
      ↑ PUBLIC/PRIVATE implementation dependency
Toy3dTaskGraph
```

CMake 约束：

```cmake
find_package(Threads REQUIRED)

add_library(Toy3dThreading STATIC)
target_compile_features(Toy3dThreading PUBLIC cxx_std_17)
set_target_properties(Toy3dThreading PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(Toy3dThreading PRIVATE Threads::Threads)

add_library(Toy3dTaskGraph STATIC)
target_compile_features(Toy3dTaskGraph PUBLIC cxx_std_17)
set_target_properties(Toy3dTaskGraph PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(Toy3dTaskGraph PUBLIC Toy3dThreading)
```

两个目标都不得依赖 runtime、renderscene、RHI backend、editor 或 shader。第一版通过返回值和注入式 diagnostics sink 报错，不强制依赖 `Toy3dLogging`。

## 5. 总体结构

```text
Composition Root
    ├── ThreadManager
    ├── TaskGraphInterface
    │   ├── Worker RunnableThread[0..N)
    │   ├── AnyWorker ready queues
    │   ├── GameThread named queue
    │   └── RenderingThread named queue
    └── Render RunnableThread
         └── attach_to_thread(RenderingThread)
             + process_thread_until_request_return()

Callers
    └── GraphTask<TaskType>::create_task(task_graph, prerequisites)
            .construct_and_dispatch_when_ready(args...)
```

边界：

- `RunnableThread` 只管理一个线程，不理解 Task；
- `TaskGraphInterface` 组合线程、queue、Event 和 GraphEvent；
- `ThreadManager` 只登记 `RunnableThread` 并提供名称、枚举和诊断，不参与 Task routing；
- `Queue` 只做非阻塞传输，不负责等待；
- `GraphTask` 不理解渲染、文件、资源或资产领域；
- Render Thread 的所有权仍由 renderer/composition root 管理，Task Graph 只把它注册为 Named Thread。

## 6. Runnable、RunnableThread 与 Thread

### 6.1 `Runnable`

公共接口与 UE 对齐：

```cpp
class Runnable
{
public:
    virtual ~Runnable() = default;

    virtual ThreadStatus init();
    virtual std::uint32_t run() = 0;
    virtual void stop();
    virtual void exit();
};
```

语义：

- `init()`、`run()`、`exit()` 在目标线程调用；
- `run()` 只在 `init()` 成功后调用；
- `stop()` 从请求停止的线程调用，必须并发安全；
- `stop()` 只设置 atomic flag 或 signal Event，不直接释放目标线程仍在使用的资源；
- `exit()` 在 `run()` 返回或抛异常后调用；
- Toy3d 捕获生命周期边界异常，不允许异常跨线程传播。

### 6.2 `RunnableThread`

```cpp
struct RunnableThreadConfig
{
    std::string name;
};

class RunnableThread final
{
public:
    static RunnableThreadCreateResult create(
        ThreadManager& thread_manager,
        std::unique_ptr<Runnable> runnable,
        RunnableThreadConfig config);

    ~RunnableThread();

    RunnableThread(const RunnableThread&) = delete;
    RunnableThread& operator=(const RunnableThread&) = delete;

    void request_stop();
    ThreadStatus wait_for_completion();

    bool joinable() const;
    std::thread::id get_thread_id() const;
    const std::string& get_thread_name() const;
    RunnableThreadState get_state() const;
    ThreadExecutionResult get_result() const;
};
```

与 UE 的一处主动差异：Toy3d 不使用容易误解为强杀的 `kill()` 名称，改为 `request_stop()`；它内部调用 `Runnable::stop()`。完成等待仍采用 UE 容易识别的 `wait_for_completion()`。

创建流程：

```text
create
  → create std::thread
  → target thread calls init()
  → target publishes init result
  → create side returns success/failure
  → run()
  → exit()
  → wait_for_completion()
```

约束：

- `RunnableThread` 独占 `Runnable`；
- `create()` 成功意味着 `init()` 已成功，不代表 `run()` 尚未结束；
- `request_stop()` 幂等；
- 禁止 self-join；
- 不提供 detach；
- 析构时仍 joinable 属于生命周期错误；
- 正式 shutdown 必须显式 request stop 并 wait for completion；
- `std::thread` 创建失败、`init()` 失败和未捕获异常写入结构化结果。

### 6.3 轻量 `Thread`

与 UE `FThread` 类似，`Thread` 是接受函数的 convenience wrapper：

```cpp
class Thread final
{
public:
    Thread(
        ThreadManager& thread_manager,
        std::string name,
        ThreadFunction function);
    ~Thread();

    bool is_joinable() const;
    void join();
    std::thread::id get_thread_id() const;
};
```

它适合测试或无需 `init/stop/exit` 的简单长任务。引擎正式服务线程优先使用 `Runnable/RunnableThread`。`Thread` 仍不提供 detach，退出通知由调用方捕获的 Event/atomic state 负责。

### 6.4 标准库边界

纯 `std::thread` 无法可移植地设置 stack size、native debugger name、priority 或 affinity。因此第一版不在 config 中暴露这些字段。name 仅作为 Toy3d diagnostics metadata。

未来只有在平台测试证明必要时才增加可选 `PlatformThreadServices` capability；Win32/POSIX 细节不得改变 `RunnableThread` 公共生命周期。

## 7. Event 与 ThreadManager

### 7.1 Event

```cpp
enum class EventMode
{
    AutoReset,
    ManualReset
};

class Event final
{
public:
    explicit Event(EventMode mode = EventMode::AutoReset);

    void trigger();
    void reset();
    void wait();
    bool wait_for(std::chrono::milliseconds timeout);
};

using EventRef = std::shared_ptr<Event>;
```

第一版使用 mutex、condition variable 和 signaled state 实现。所有 wait 使用 predicate。Auto Reset 每个 trigger 至多释放一个 waiter，并在无 waiter 时保留一个 signal；Manual Reset 在 reset 前释放全部当前与后续 waiter。

### 7.2 ThreadManager

```cpp
struct ThreadInfo
{
    std::thread::id id;
    std::string name;
    RunnableThreadState state = RunnableThreadState::Created;
};

class ThreadManager final
{
public:
    ThreadInfo get_thread(std::thread::id id) const;
    std::string get_thread_name(std::thread::id id) const;
    void for_each_thread(
        const std::function<void(const ThreadInfo&)>& function) const;

private:
    friend class RunnableThread;

    void add_thread(RunnableThread& thread);
    void remove_thread(RunnableThread& thread);
};
```

- `RunnableThread::create()` 成功建立底层线程后自动调用 `add_thread()`；完成等待并释放线程对象前调用 `remove_thread()`；
- `ThreadManager` 保存 thread ID 到 `RunnableThread*` 的 non-owning 映射；线程所有权仍属于创建者或 Task Graph；
- `add_thread()`/`remove_thread()` 只允许 `RunnableThread` 调用，业务模块不能伪造登记；
- `Thread` convenience 内部复用 `RunnableThread`，因此自动进入同一登记表；
- Game Thread 如果不是 `RunnableThread`，不需要登记到 `ThreadManager`；它的 Named Thread 身份由 Task Graph 管理；
- `ThreadManager` 不保存 `NamedThread`、不提供 attach、不决定 Game/Render/Worker routing；
- `ThreadManager` 不使用全局 singleton，由 composition root 持有并注入；
- 枚举结果用于 diagnostics、profiler 和 shutdown 检查，不授予被枚举线程的所有权。

## 8. NamedThread 与 Task priority

```cpp
enum class NamedThread
{
    Unknown,
    GameThread,
    RenderingThread,
    AnyWorker
};

enum class TaskPriority
{
    Normal,
    High
};

enum class SubsequentsMode
{
    TrackSubsequents,
    FireAndForget
};
```

第一版不把 thread index、queue index、thread priority 和 task priority 位打包到一个整数中。UE 的 bit encoding 是规模与历史需求，不是公共语义本身。

Task Graph 提供：

```cpp
NamedThread get_render_thread() const;
```

多线程模式返回 `RenderingThread`；单线程模式返回 `GameThread`。上层通过 logical render thread 查询路由，不散布配置判断。

High Task 只优先于尚未运行的 Normal Task，不能抢占 running Task。单 worker pool 在连续执行一定数量 High Task 后必须尝试 Normal Task，避免饥饿。

## 9. Queue 与无锁边界

### 9.1 UE 风格公共 Queue

```cpp
enum class QueueMode
{
    Spsc,
    Mpsc
};

template<typename T, QueueMode Mode = QueueMode::Spsc>
class Queue final
{
public:
    bool enqueue(T value);
    bool dequeue(T& value);
    bool is_empty() const;
};
```

contract：

- SPSC：单 producer、单 consumer；
- MPSC：多 producer、单 consumer；
- 只有 owner consumer 可以调用 `dequeue()`；
- linked dummy-node 实现允许 consumer 安全回收旧 tail，无需 hazard pointer；
- enqueue 为元素分配 node，因此是无界、运行期分配的容器；
- 上层必须提供 outstanding budget/backpressure；
- `is_empty()` 只是 consumer 的瞬时观测；
- 销毁前所有 producer/consumer 必须退出。

固定容量、零分配的一对一流式传输若出现明确用例，可以再增加 `CircularQueue<T>`，对应 UE `TCircularQueue`；第一版不同时提供语义重叠的多个 SPSC 类型。

### 9.2 内部 `BoundedMpmcQueue`

Any Worker ready queue 需要 MPMC。内部实现采用固定 2 的幂容量、per-slot sequence counter 的 bounded queue：

- 多 producer、多 consumer；
- 不做动态节点回收，避免 ABA/hazard pointer；
- 第一版只承载 `BaseGraphTask*`；
- capacity 等于 `max_tasks_in_flight`；
- Task Graph 在创建 Task 前取得 outstanding budget，保证已接受 ready Task 不因 queue full 丢失；
- 只有出现第二个明确 MPMC 用例并通过独立压力测试后，才考虑提升为公共容器。

### 9.3 `StallingTaskQueue`

`StallingTaskQueue` 是 Task Graph 私有调度结构，对应 UE `FStallingTaskQueue` 的职责，而不是公共容器：

```text
High BoundedMpmcQueue
Normal BoundedMpmcQueue
sleep/wake generation
sleeping worker state
condition variable/Event
```

queue 操作本身 lock-free；睡眠状态使用短 mutex 临界区。worker 在 sleep 前必须重查 queue 和 generation，enqueue 在 publish 后更新 generation 并 notify，避免 lost wakeup。空闲 worker 不 busy spin。

## 10. GraphEvent

```cpp
class GraphEvent;

using GraphEventRef = std::shared_ptr<GraphEvent>;
using GraphEventArray = std::vector<GraphEventRef>;

enum class TaskOutcome
{
    Pending,
    Succeeded,
    Failed,
    Cancelled
};

class GraphEvent final
{
public:
    static GraphEventRef create_graph_event();

    bool is_complete() const;
    TaskOutcome get_outcome() const;
    void wait(
        TaskGraphInterface& task_graph,
        NamedThread current_thread = NamedThread::Unknown) const;

    void dont_complete_until(GraphEventRef event);
};
```

`GraphEvent` 职责与 UE 一致：

- 表达一个 TrackSubsequents Task 的 completion；
- 保存等待它的 subsequent Task；
- completion 时 atomically close subsequent list；
- 对每个 subsequent 递减 prerequisite count；
- 计数降为零的 Task 被路由到目标 queue；
- reference-counted handle 可以跨调用方共享；
- `is_complete()` 为 acquire 观测；terminal publication 为 release。

第一版 subsequent list 使用 mutex + closed flag，而不是复制 UE closable lock-free list。依赖注册尚未证明是热点；短临界区更容易证明生命周期正确。

`dont_complete_until()` 只能在 owning Task 的 `do_task()` 执行期间调用。Task callable 返回后：

- 没有额外 event 时立即 dispatch subsequents；
- 有额外 event 时创建内部 gather Task，等待它们完成后再关闭当前 GraphEvent；
- FireAndForget Task 没有 GraphEvent，不能调用此函数。

Toy3d 的小扩展是 `TaskOutcome`：Task 异常被捕获后记录 `Failed`，但 GraphEvent 仍然完成并唤醒 dependents。prerequisite 默认只表达顺序，不自动传播业务失败。

## 11. GraphTask 公共写法

### 11.1 TaskType contract

用户 Task 与 UE 风格一致：

```cpp
class PrepareViewTask final
{
public:
    explicit PrepareViewTask(PreparedViewInput input);

    static NamedThread get_desired_thread()
    {
        return NamedThread::AnyWorker;
    }

    static SubsequentsMode get_subsequents_mode()
    {
        return SubsequentsMode::TrackSubsequents;
    }

    static TaskPriority get_priority()
    {
        return TaskPriority::Normal;
    }

    void do_task(
        NamedThread current_thread,
        const GraphEventRef& completion_event);

private:
    PreparedViewInput input_;
};
```

TaskType 必须：

- 可在 `GraphTask<TaskType>` 内直接构造；
- 提供 `get_desired_thread()`；
- 提供 `get_subsequents_mode()`；
- 提供 `get_priority()`；
- 提供 `do_task(current_thread, completion_event)`；
- 不保存可能提前销毁的栈引用；
- 不依赖 UE stats 宏；diagnostic name 由可选静态函数或 dispatch 参数提供。

### 11.2 创建与 dispatch

```cpp
GraphEventArray prerequisites{visibility_event, resource_event};

GraphEventRef prepare_event =
    GraphTask<PrepareViewTask>::create_task(
        task_graph,
        &prerequisites,
        current_thread)
    .construct_and_dispatch_when_ready(std::move(input));
```

公共 helper：

```cpp
template<typename TaskType>
class GraphTask final
{
public:
    class Constructor final
    {
    public:
        template<typename... Args>
        GraphEventRef construct_and_dispatch_when_ready(Args&&... args);

        template<typename... Args>
        GraphTask* construct_and_hold(Args&&... args);
    };

    static Constructor create_task(
        TaskGraphInterface& task_graph,
        const GraphEventArray* prerequisites = nullptr,
        NamedThread current_thread = NamedThread::Unknown);

    void unlock(NamedThread current_thread = NamedThread::Unknown);
    GraphEventRef get_completion_event() const;
};
```

这组命名有意与 UE `TGraphTask` 保持对应。与 UE 的必要差异是 `create_task()` 显式接收 `TaskGraphInterface&`，因为 Toy3d 禁止全局 singleton。

`construct_and_hold()` 只用于必须先构造、随后由明确控制点 `unlock()` 的少数协议；普通调用统一使用 `construct_and_dispatch_when_ready()`。
返回的 `GraphTask*` 是 Task Graph 拥有的临时非 owning 指针，只允许调用 `unlock()` 或查询 completion；调用方不得保存、释放或在 unlock 后再次访问。

### 11.3 函数式 convenience

为不值得定义 TaskType 的短函数提供：

```cpp
using GraphTaskFunction = std::function<void(
    NamedThread current_thread,
    const GraphEventRef& completion_event)>;

GraphEventRef dispatch_graph_task(
    TaskGraphInterface& task_graph,
    std::string debug_name,
    GraphTaskFunction function,
    NamedThread desired_thread = NamedThread::AnyWorker,
    const GraphEventArray* prerequisites = nullptr,
    SubsequentsMode mode = SubsequentsMode::TrackSubsequents);
```

底层仍创建普通 GraphTask，不建立第二套调度语义。需要复用、数据成员或明确领域名称的 Task 应定义 TaskType。

## 12. TaskGraphInterface

公共接口采用 UE 容易识别的方法集合：

```cpp
class TaskGraphInterface
{
public:
    virtual ~TaskGraphInterface() = default;

    virtual NamedThread get_current_thread_if_known() const = 0;
    virtual NamedThread get_render_thread() const = 0;
    virtual std::uint32_t get_num_worker_threads() const = 0;
    virtual bool is_thread_processing_tasks(
        NamedThread thread) const = 0;

    virtual TaskGraphStatus attach_to_thread(
        NamedThread current_thread) = 0;
    virtual std::uint64_t process_thread_until_idle(
        NamedThread current_thread) = 0;
    virtual void process_thread_until_request_return(
        NamedThread current_thread) = 0;
    virtual void request_return(NamedThread current_thread) = 0;

    virtual TaskWaitResult wait_until_tasks_complete(
        const GraphEventArray& tasks,
        NamedThread current_thread = NamedThread::Unknown) = 0;
    virtual void trigger_event_when_tasks_complete(
        Event& event,
        const GraphEventArray& tasks,
        NamedThread current_thread = NamedThread::Unknown) = 0;

    virtual void wake_named_thread(NamedThread thread) = 0;
    virtual TaskGraphShutdownResult shutdown(
        TaskGraphShutdownMode mode) = 0;
};
```

convenience：

```cpp
TaskWaitResult wait_until_task_completes(
    const GraphEventRef& task,
    NamedThread current_thread = NamedThread::Unknown);
```

创建由 factory 完成：

```cpp
struct TaskGraphConfig
{
    // Zero selects an automatic count from the composition-root policy.
    std::uint32_t worker_thread_count = 0;
    std::uint32_t max_tasks_in_flight = 4096;
    bool multithreaded = true;
};

TaskGraphCreateResult create_task_graph(
    TaskGraphConfig config,
    ThreadManager& thread_manager,
    TaskGraphDiagnostics* diagnostics = nullptr);
```

factory 返回 composition root 独占的 `std::unique_ptr<TaskGraphInterface>`。不提供 `TaskGraphInterface::get()`、`GTaskGraph` 或静态隐式初始化。
注入的 `ThreadManager` 只供 Task Graph 创建和登记 worker `RunnableThread`。`attach_to_thread()` 使用 Task Graph 自己的 TLS 建立 Named Thread binding，不经过 `ThreadManager`。

## 13. 调度、等待与 single-thread fallback

### 13.1 路由

- `NamedThread::AnyWorker`：进入 internal High/Normal `StallingTaskQueue`；
- `NamedThread::GameThread`：进入 Game named MPSC queue；
- `NamedThread::RenderingThread`：进入 Render named MPSC queue；
- Named Thread 未 attach 时，定向 dispatch 返回/记录 `TargetUnavailable`，不得在错误线程执行；
- `multithreaded=false` 时，AnyWorker 与 logical Render target 都路由到 Game Thread FIFO；
- Task 的 desired thread 在构造完成后固定，运行期不漂移。

### 13.2 prerequisite/subsequent

```text
create GraphTask
  → register with each prerequisite GraphEvent
  → remaining_prerequisites == 0 ? queue : wait
  → execute do_task()
  → destroy TaskType payload
  → satisfy dont_complete_until events if any
  → close completion GraphEvent
  → decrement subsequents
```

注册 subsequent 时若 prerequisite 已经 closed，注册返回 false，GraphTask 立即抵扣该 prerequisite。这个 closed/register 协议解决“检查完成”和“注册依赖”的竞态。

dispatch 后不允许追加 prerequisite。`dont_complete_until()` 只延迟当前 completion，不改变 Task 自身已经开始执行的事实。

### 13.3 等待时 helping

`wait_until_tasks_complete()` 根据当前 thread identity 选择策略：

- Game Thread：pump Game named queue，并可有限帮助 AnyWorker Task；
- Rendering Thread：pump Render named queue，默认不执行无亲和性的重 worker Task；
- worker：执行其他 ready worker Task，直到目标完成；
- Unknown/External：阻塞在 Event；
- single-thread mode：Game Thread pump 所有映射到自己的 Task；递归不可取得进展时诊断 fatal/deadlock risk。

每轮 helping 只执行有限批次，随后重查目标 completion、shutdown 和 timeout。禁止持有业务锁时 wait。self-wait、依赖环和唯一 Named Thread 等待只能由自己运行的不可达 Task，必须在 Debug 构建尽早诊断。

timeout 只结束等待，不取消 Task。

### 13.4 Task 粒度

- Task 应短、CPU-bound、可以独立完成；
- 持续服务使用 `RunnableThread`；
- 阻塞 I/O 不进入普通 worker；
- 批量循环由上层切成约 `worker_count * 2..4` 个 chunk；
- 第一版不提供 `parallel_for`；出现两个稳定调用方后再作为 GraphTask helper 增加；
- Task 不得捕获线程归属对象的无保护裸指针；跨线程 payload 必须 owned 或具有明确共享所有权。

## 14. 所有权与生命周期

### 14.1 Composition root 顺序

```text
create ThreadManager
create TaskGraphInterface(ThreadManager&) and worker RunnableThreads
task_graph.attach_to_thread(GameThread)
create Render RunnableThread(ThreadManager&)
Render init:
    attach RenderingThread
    initialize renderer-owned state
Render run:
    process_thread_until_request_return(RenderingThread)
run engine
stop domain submissions
drain/cancel graph tasks
request_return(RenderingThread)
request Render stop
wait_for_completion(Render)
shutdown TaskGraph (join workers and invalidate Named Thread TLS)
destroy TaskGraphInterface
destroy ThreadManager
```

所有权：

- composition root 独占 `TaskGraphInterface`；
- Task Graph 独占 worker `RunnableThread`；
- renderer/composition root 独占 Render `RunnableThread`；
- GraphTask 在完成时销毁 TaskType payload；
- `GraphEventRef` 共享 completion state；
- terminal publication 后释放 callable、prerequisite 与 subsequent 临时存储；
- Task Graph 不拥有 Game/Render 外部线程，也不负责销毁 renderer 领域对象。

### 14.2 Shutdown

```cpp
enum class TaskGraphShutdownMode
{
    Drain,
    CancelPending
};
```

- `Drain`：停止接受新 Task，要求 Named Thread 继续 pump，完成全部已接受 Task；
- `CancelPending`：未运行 Task 发布 `Cancelled` completion；running Task 不被强杀，仍等待返回；
- 第一版没有日常单 Task cancel API；CancelPending 只用于 graph teardown；
- 所有已接受 TrackSubsequents Task 必须发布一次 completion；
- FireAndForget Task 也必须完成内部生命周期和统计；
- shutdown 后 create/dispatch 返回 `Stopped`；
- Task Graph 析构不猜测 shutdown 策略，未显式 shutdown 属于生命周期错误；
- diagnostics callback、用户 Task 和 subsequent dispatch 不在 control mutex 下调用。

## 15. 错误模型

线程错误至少包括：

```text
InvalidState
InvalidConfig
CreateFailed
InitFailed
InvalidCaller
UnhandledException
NotJoined
```

Task Graph 错误至少包括：

```text
InvalidGraphEvent
InvalidPrerequisite
TargetUnavailable
Overloaded
Stopped
Timeout
Cancelled
TaskFailed
DeadlockRisk
```

规则：

- create、dispatch、wait、attach 和 shutdown 返回结构化 status；
- Task dispatch 被接受后不得静默丢失；
- saturation 返回 `Overloaded`，不在提交线程偷偷同步执行；
- TaskType::do_task 抛出的异常在 executor 边界捕获，completion outcome 为 `Failed`；
- 异常不跨线程传播；
- failure 默认不取消 subsequent；prerequisite 只表达执行顺序；
- diagnostics 包含 Task debug name、desired thread、current thread 和状态；
- completion outcome 先写入，再 release 发布 complete；读取使用 acquire。

## 16. 线程与内存模型

- `Runnable::stop()` 使用的 stop flag 由具体 runnable 以 release/acquire 管理；
- Queue enqueue 是所有权 publish 点，dequeue 是 acquire 点；
- GraphTask completion 先发布 outcome，再关闭 GraphEvent；
- subsequent 注册由 GraphEvent mutex + closed flag 串行化；
- ready Task 的 pointer 通过 bounded MPMC sequence slot 发布；
- 只有不参与跨字段 invariant 的统计计数可以使用 relaxed atomic；
- 每处非默认 memory order 必须注释对应的 happens-before；
- cache-line padding 仅用于经过压力测试确认的共享 atomic 热点；
- queue、Task Graph 和 ThreadManager 销毁前必须完成明确 shutdown。

## 17. 平台差异

| 平台 | 第一版实现 | 重点验证 |
| --- | --- | --- |
| Windows | MSVC C++17 std threading | create failure、随机延迟压力、VS 可用 sanitizer |
| macOS | libc++ C++17 std threading | TSAN、App lifecycle、hardware concurrency |
| Linux | libstdc++/libc++ C++17 | TSAN、高核心数 contention、spurious wakeup |
| Android | NDK libc++ C++17 | 小核心数、前后台切换、worker 上限 |

默认 worker 数由 composition root 计算：

```text
logical = hardware_concurrency or configured_fallback
reserved = Game + Render
workers = clamp(logical - reserved, configured_min, configured_max)
```

`std::thread::hardware_concurrency()` 返回零时使用配置 fallback。移动端通过 config 降低 worker max，Task Graph 不读取 RHI backend 或 OS 名称猜策略。

## 18. 安全边界

- 禁止强杀线程，避免锁、TLS、allocator 和 RHI state 泄漏；
- `max_tasks_in_flight` 限制 graph 内存；
- unbounded MPSC named queue 受同一 outstanding budget 约束；
- Task debug name 不解释为格式串、路径或命令；
- 用户 Task 不在 graph control mutex 下执行；
- CancelPending 不释放 running Task 仍可能访问的 payload；
- shutdown 唤醒全部 waiter；
- 每个已接受 TrackSubsequents Task 恰好关闭一次 GraphEvent；
- GraphEvent 只保证调度与完成同步，payload 自身并发安全仍由领域 contract 负责。

## 19. 可观测性

第一版保留：

- registered thread name、id、state、start/finish time；
- Task Graph 当前 `NamedThread` binding 与对应 thread id；
- Task debug name、desired thread、priority、dispatch/start/finish timestamp；
- ready queue 估算值和 high-water mark；
- created/dispatched/completed/failed/cancelled/rejected 计数；
- active/sleeping worker 数；
- wait、helped Task、timeout 次数；
- shutdown 未完成 Task 列表。

统计关闭不得改变同步语义。高频 timestamp 可以配置关闭；字符串 intern 优化后置。

## 20. 测试矩阵

### 20.1 RunnableThread/Event

- `init/run/exit` 顺序与执行线程；
- init handshake、init failure 和 lifecycle exception；
- stop 幂等、阻塞 runnable 唤醒；
- wait for completion、重复 wait、self-join、未 join 析构诊断；
- Auto/Manual Reset Event 的 signal-before-wait、release-one/all、reset、timeout；
- spurious wakeup 和 shutdown race。

### 20.2 Queue/StallingTaskQueue

- SPSC 与 MPSC exactly-once；
- MPSC 每 producer 内 FIFO；
- 非 owner consumer 的 Debug 诊断；
- bounded MPMC 多 producer/consumer；
- capacity、sequence wrap 缩小位宽测试；
- producer 暂停在 publish 中间的压力测试；
- sleep/check/enqueue 交错不丢 wakeup；
- 长时间随机延迟；
- macOS/Linux TSAN。

### 20.3 GraphEvent/GraphTask

- 无 prerequisite、单 prerequisite、diamond、fan-in、fan-out；
- prerequisite 在注册前/中/后完成；
- TrackSubsequents 与 FireAndForget；
- `dont_complete_until()` 一个/多个 child event；
- construct-and-hold + unlock；
- TaskType 只构造/执行/析构一次；
- Task exception 仍关闭 GraphEvent；
- completion outcome 的 release/acquire 可见性；
- external `GraphEventRef` 长期持有不保留重 Task payload。

### 20.4 TaskGraphInterface

- Game/Rendering/AnyWorker 精确路由；
- Named Thread attach、重复 attach、错误线程 pump；
- Game wait 时 pump/help；Render wait 时只 pump Render；worker wait 时 help；
- Unknown thread wait 使用 Event；
- High/Normal 和防饥饿；
- max_tasks_in_flight saturation；
- Drain/CancelPending；
- single-thread fallback 确定性 FIFO；
- self-wait、cycle 与 deadlock-risk 诊断；
- 反复 create/shutdown 无 Thread、GraphTask、GraphEvent 或 node 泄漏；
- 集成用例：两个 AnyWorker prepare Task 完成后，RenderingThread Task 执行。

## 21. 实施与迁移顺序

### 批次 A：Toy3dThreading

1. 建立 `Toy3dThreading`；
2. 实现 Runnable、RunnableThread、Thread；
3. 实现 Event 与 ThreadManager；
4. 完成生命周期、异常与 event 测试；
5. 不接入 runtime。

### 批次 B：Queue

1. 实现 `Queue<T, QueueMode::Spsc/Mpsc>`；
2. 实现 internal `BoundedMpmcQueue<BaseGraphTask*>`；
3. 完成 exactly-once、FIFO、析构、随机延迟和 sanitizer 测试。

### 批次 C：GraphEvent 与 GraphTask

1. 建立 `Toy3dTaskGraph`；
2. 实现 GraphEvent、BaseGraphTask、GraphTask；
3. 实现 prerequisite/subsequent 与 `dont_complete_until()`；
4. 完成纯单线程 DAG 测试。

### 批次 D：TaskGraph scheduler

1. 实现 StallingTaskQueue 和 worker RunnableThread；
2. 实现 Named Thread attach/pump；
3. 实现 wait-time helping、priority、single-thread fallback；
4. 实现 saturation、异常和 shutdown；
5. 完成完整 Task Graph 测试。

### 批次 E：引擎迁移

1. 先用 fake Game/Render loop 验证 Named Thread；
2. 将未来 Render Thread 建立为 `Runnable/RunnableThread`；
3. 在 Render Thread init 中 attach `RenderingThread`；
4. 选择一个 immutable-input CPU prepare 用例接入 GraphTask；
5. 按独立批次迁移旧的直接 `std::thread` 和局部调度入口；
6. frame lag、bounded frame transport、flush 和 renderer shutdown 继续保留在渲染领域。

删除旧入口的条件：

- 所有生产调用方已经迁移并通过生命周期测试；
- 不再存在 Task Graph 与另一套正式短任务系统双轨；
- runtime/editor/tools 的直接 `std::thread` 仅限批准的平台特殊实现；
- Render Thread、worker、Named Thread 和 shutdown 路径都有回归证据；
- saturation 和 failure 路径不丢 completion。

## 22. 后续演进门槛

只有出现证据后才增加：

- work stealing：central ready queue contention 已被 profile 证明是瓶颈；
- small-task/slab allocator：GraphTask allocation 进入帧时间热点；
- closable lock-free subsequent list：GraphEvent mutex 成为实际热点；
- blocking executor：文件、进程或网络已有稳定 blocking contract；
- `parallel_for`：至少两个模块需要相同 chunk 策略；
- typed Future：至少两个调用方需要相同返回值所有权；
- native thread priority/affinity：平台证据证明默认调度不足；
- fiber/coroutine：同步等待确实造成利用率问题且工具链允许；
- public MPMC queue：Task Graph 外出现第二个明确 MPMC 用例。

这些演进不得改变 Runnable lifecycle、GraphEvent completion、GraphTask prerequisite、Named Thread routing 和显式 Task Graph 注入五项长期 contract。

## 23. 最终骨架

```text
Runnable / RunnableThread
    UE-style init → run → exit
    cooperative stop
    target-thread init handshake
    explicit wait_for_completion

Thread
    lightweight function thread
    explicit join

TaskGraphInterface
    GraphTask<TaskType>
    GraphEventRef prerequisite/completion
    GameThread / RenderingThread / AnyWorker
    wait-time pump/help
    single-thread fallback

Queue
    Queue<T, QueueMode::Spsc/Mpsc>
    internal BoundedMpmcQueue<BaseGraphTask*>
    internal StallingTaskQueue
```

这套设计在调用方式、术语和职责上与 UE4.27 Task Graph 保持直接对应，同时删除 Toy3d 当前不需要的全局服务、平台复杂度和历史兼容负担。
