# Threading：线程、队列与 TaskGraph

## 定位

Toy3dThreading：engine/core/threading 的 Thread/Event/RunnableThread/ThreadManager/容器；Toy3dTaskGraph：engine/core/task_graph 的 TaskGraph/GraphTask/GraphEvent/NamedThread，传递依赖 Threading。Game/Render 策略在 [Render Framework](render-framework.md)。

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
- 同线程等待使用 scheduler 已知线程处理入口，不能消费者等自己后面的任务；Unknown 等待不授予 NamedThread 权限。
- 不持业务锁等待任务；长期阻塞进程/I/O 用专用线程，不占 worker。
- 按现有 TaskGraphStatus/Exception 处理失败，不虚构 enqueue bool/全局 pool API。

## Shutdown 与验证

先停止生产，再按 shutdown mode 处理已接受任务与依赖；渲染 drain 后才关闭 TaskGraph。Event/root/payload 必须覆盖消费者生命周期。

改动覆盖依赖未完成、dispatch 失败、worker 异常、同线程等待、重复关闭、停止中 enqueue、队列满、析构线程及 SingleThread。构建相关 core target/测试；渲染调用补验证 RenderCommand/Fence，不仅测 scheduler。
