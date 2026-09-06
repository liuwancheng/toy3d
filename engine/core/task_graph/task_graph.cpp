#include "task_graph/task_graph.h"

#include "task_graph/base_graph_task.h"
#include "task_graph/stalling_task_queue.h"
#include "threading/containers/queue.h"
#include "threading/runnable.h"
#include "threading/runnable_thread.h"
#include "threading/thread_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        constexpr std::uint32_t default_worker_fallback = 2;
        constexpr std::uint32_t maximum_automatic_workers = 32;
        constexpr std::uint32_t wait_help_batch_size = 16;
        constexpr std::chrono::milliseconds wait_poll_interval(1);

        std::mutex active_task_graph_mutex;
        TaskGraphInterface* active_task_graph = nullptr;
        bool task_graph_creation_in_progress = false;

        bool reserve_active_task_graph_creation()
        {
            std::lock_guard<std::mutex> lock(active_task_graph_mutex);
            if (active_task_graph != nullptr || task_graph_creation_in_progress)
            {
                return false;
            }
            task_graph_creation_in_progress = true;
            return true;
        }

        void cancel_active_task_graph_creation()
        {
            std::lock_guard<std::mutex> lock(active_task_graph_mutex);
            task_graph_creation_in_progress = false;
        }

        void publish_active_task_graph(TaskGraphInterface& task_graph)
        {
            std::lock_guard<std::mutex> lock(active_task_graph_mutex);
            active_task_graph = &task_graph;
            task_graph_creation_in_progress = false;
        }

        void unpublish_active_task_graph(TaskGraphInterface& task_graph)
        {
            std::lock_guard<std::mutex> lock(active_task_graph_mutex);
            if (active_task_graph == &task_graph)
            {
                active_task_graph = nullptr;
            }
        }

        std::size_t round_up_queue_capacity(std::uint32_t requested)
        {
            std::size_t capacity = 2;
            while (capacity < requested)
            {
                if (capacity > std::numeric_limits<std::size_t>::max() / 2)
                {
                    throw std::overflow_error("Task Graph queue capacity overflowed");
                }
                capacity *= 2;
            }
            return capacity;
        }

        std::uint32_t select_worker_count(const TaskGraphConfig& config)
        {
            if (!config.multithreaded)
            {
                return 0;
            }
            if (config.worker_thread_count != 0)
            {
                return config.worker_thread_count;
            }

            const std::uint32_t logical_threads = std::thread::hardware_concurrency();
            if (logical_threads == 0)
            {
                return default_worker_fallback;
            }
            if (logical_threads <= 2)
            {
                return 1;
            }
            const std::uint32_t available = logical_threads - 2;
            return available < maximum_automatic_workers ? available : maximum_automatic_workers;
        }

        bool all_tasks_complete(const GraphEventArray& tasks)
        {
            return std::all_of(tasks.begin(), tasks.end(),
                               [](const GraphEventRef& task) { return task && task->is_complete(); });
        }
    } // namespace

    class TaskGraph;

    class TaskGraphWorkerRunnable final : public Runnable
    {
      public:
        TaskGraphWorkerRunnable(TaskGraph& task_graph, std::uint32_t worker_index)
            : task_graph_(task_graph), worker_index_(worker_index)
        {
        }

        ThreadStatus init() override;
        std::uint32_t run() override;
        void stop() override;
        void exit() override;

      private:
        TaskGraph& task_graph_;
        std::uint32_t worker_index_ = 0;
    };

    class TaskGraph final : public TaskGraphInterface
    {
      public:
        TaskGraph(TaskGraphConfig config, ThreadManager& thread_manager, TaskGraphDiagnostics diagnostics)
            : config_(config), thread_manager_(thread_manager), diagnostics_(std::move(diagnostics)),
              worker_thread_count_(select_worker_count(config)),
              worker_queue_(round_up_queue_capacity(config.max_tasks_in_flight))
        {
        }

        ~TaskGraph() override
        {
            if (!shutdown_complete_.load())
            {
                report(TaskGraphStatus::failure(TaskGraphErrorCode::InvalidState,
                                                "Task Graph was destroyed before explicit shutdown"));
                shutdown(TaskGraphShutdownMode::CancelPending);
            }
        }

        TaskGraphStatus start_workers()
        {
            worker_threads_.reserve(worker_thread_count_);
            for (std::uint32_t index = 0; index < worker_thread_count_; ++index)
            {
                RunnableThreadCreateResult created =
                    RunnableThread::create(thread_manager_, std::make_unique<TaskGraphWorkerRunnable>(*this, index),
                                           {"TaskGraphWorker" + std::to_string(index)});
                if (!created.succeeded())
                {
                    worker_stop_requested_.store(true);
                    worker_queue_.stop();
                    stop_and_join_workers();
                    return TaskGraphStatus::failure(TaskGraphErrorCode::ThreadCreateFailed, created.status().message);
                }
                worker_threads_.push_back(created.take_thread());
            }
            return TaskGraphStatus::success();
        }

        NamedThread get_current_thread_if_known() const override
        {
            if (tls_graph_ == this)
            {
                return tls_thread_;
            }
            return NamedThread::Unknown;
        }

        NamedThread get_render_thread() const override
        {
            return config_.multithreaded ? NamedThread::RenderingThread : NamedThread::GameThread;
        }

        std::uint32_t get_num_worker_threads() const override { return worker_thread_count_; }

        bool is_thread_processing_tasks(NamedThread thread) const override
        {
            if (thread == NamedThread::GameThread)
            {
                return game_processing_.load();
            }
            if (thread == NamedThread::RenderingThread)
            {
                return render_processing_.load();
            }
            if (thread == NamedThread::AnyWorker)
            {
                return active_worker_tasks_.load() != 0;
            }
            return false;
        }

        TaskGraphStatus attach_to_thread(NamedThread current_thread) override
        {
            if (current_thread != NamedThread::GameThread && current_thread != NamedThread::RenderingThread)
            {
                return failure(TaskGraphErrorCode::InvalidCaller,
                               "Only GameThread or RenderingThread can be attached externally");
            }
            if (!config_.multithreaded && current_thread == NamedThread::RenderingThread)
            {
                return failure(TaskGraphErrorCode::InvalidCaller,
                               "Single-thread Task Graph maps rendering work to GameThread");
            }
            if (tls_graph_ != nullptr)
            {
                return failure(TaskGraphErrorCode::InvalidState,
                               "The current thread is already attached to a Task Graph");
            }

            std::unique_lock<std::mutex> lock(binding_mutex_);
            std::thread::id& binding = current_thread == NamedThread::GameThread ? game_thread_id_ : render_thread_id_;
            if (binding != std::thread::id{})
            {
                lock.unlock();
                return failure(TaskGraphErrorCode::InvalidState, "The requested Named Thread already has an owner");
            }
            binding = std::this_thread::get_id();
            tls_graph_ = this;
            tls_thread_ = current_thread;
            tls_high_priority_streak_ = 0;
            return TaskGraphStatus::success();
        }

        std::uint64_t process_thread_until_idle(NamedThread current_thread) override
        {
            if (!validate_named_thread_caller(current_thread).succeeded())
            {
                return 0;
            }

            std::atomic<bool>& processing =
                current_thread == NamedThread::GameThread ? game_processing_ : render_processing_;
            processing.store(true);
            std::uint64_t processed = 0;
            while (process_one_named_task(current_thread))
            {
                ++processed;
            }
            processing.store(false);
            return processed;
        }

        void process_thread_until_request_return(NamedThread current_thread) override
        {
            if (!validate_named_thread_caller(current_thread).succeeded())
            {
                return;
            }

            std::atomic<bool>& return_requested =
                current_thread == NamedThread::GameThread ? game_return_requested_ : render_return_requested_;
            std::atomic<bool>& processing =
                current_thread == NamedThread::GameThread ? game_processing_ : render_processing_;
            Event& wake_event = current_thread == NamedThread::GameThread ? game_wake_event_ : render_wake_event_;
            if (return_requested.exchange(false))
            {
                return;
            }
            processing.store(true);
            while (!return_requested.load())
            {
                if (!process_one_named_task(current_thread))
                {
                    wake_event.wait();
                }
            }
            processing.store(false);
            return_requested.store(false);
        }

        void request_return(NamedThread current_thread) override
        {
            if (current_thread == NamedThread::GameThread)
            {
                game_return_requested_.store(true);
                game_wake_event_.trigger();
            }
            else if (current_thread == NamedThread::RenderingThread)
            {
                render_return_requested_.store(true);
                render_wake_event_.trigger();
            }
            else
            {
                failure(TaskGraphErrorCode::InvalidCaller, "request_return requires a Named Thread target");
            }
        }

        TaskWaitResult wait_until_tasks_complete(const GraphEventArray& tasks, NamedThread current_thread) override
        {
            for (const GraphEventRef& task : tasks)
            {
                if (!task)
                {
                    return {
                        failure(TaskGraphErrorCode::InvalidGraphEvent, "Task Graph wait received a null GraphEvent")};
                }
                if (tls_current_task_ != nullptr && tls_current_task_->get_completion_event() == task)
                {
                    return {failure(TaskGraphErrorCode::DeadlockRisk,
                                    "A GraphTask cannot wait for its own completion event")};
                }
            }
            if (tasks.empty())
            {
                return {TaskGraphStatus::success()};
            }

            const NamedThread known_thread = get_current_thread_if_known();
            if (current_thread == NamedThread::Unknown)
            {
                current_thread = known_thread;
            }
            else if (known_thread != current_thread)
            {
                return {failure(TaskGraphErrorCode::InvalidCaller,
                                "Task Graph wait caller does not own the supplied Named Thread")};
            }

            auto completed_event = std::make_shared<Event>(EventMode::ManualReset);
            auto remaining = std::make_shared<std::atomic<std::size_t>>(tasks.size());
            auto completed = [completed_event, remaining]()
            {
                if (remaining->fetch_sub(1) == 1)
                {
                    completed_event->trigger();
                }
            };
            for (const GraphEventRef& task : tasks)
            {
                if (!task->add_subsequent(completed))
                {
                    completed();
                }
            }
            while (!all_tasks_complete(tasks))
            {
                bool progressed = false;
                for (std::uint32_t index = 0; index < wait_help_batch_size; ++index)
                {
                    if (!help_one_task(current_thread))
                    {
                        break;
                    }
                    progressed = true;
                }
                if (!progressed)
                {
                    if (single_thread_cannot_make_progress(current_thread))
                    {
                        return {failure(TaskGraphErrorCode::DeadlockRisk,
                                        "Single-thread Task Graph wait has no runnable work")};
                    }
                    completed_event->wait_for(wait_poll_interval);
                }
            }
            // The callback owns the shared Event until trigger() returns, so a waiter cannot
            // destroy synchronization state while terminal publication is still notifying it.
            completed_event->wait();

            for (const GraphEventRef& task : tasks)
            {
                if (task->get_outcome() == TaskOutcome::Failed)
                {
                    return {failure(TaskGraphErrorCode::TaskFailed, "At least one waited GraphTask failed")};
                }
                if (task->get_outcome() == TaskOutcome::Cancelled)
                {
                    return {failure(TaskGraphErrorCode::Cancelled, "At least one waited GraphTask was cancelled")};
                }
            }
            return {TaskGraphStatus::success()};
        }

        void trigger_event_when_tasks_complete(Event& event, const GraphEventArray& tasks, NamedThread) override
        {
            if (tasks.empty())
            {
                event.trigger();
                return;
            }

            auto remaining = std::make_shared<std::atomic<std::size_t>>(tasks.size());
            auto completed = [&event, remaining]()
            {
                if (remaining->fetch_sub(1) == 1)
                {
                    event.trigger();
                }
            };
            for (const GraphEventRef& task : tasks)
            {
                if (!task)
                {
                    failure(TaskGraphErrorCode::InvalidGraphEvent, "Completion trigger received a null GraphEvent");
                    completed();
                }
                else if (!task->add_subsequent(completed))
                {
                    completed();
                }
            }
        }

        void wake_named_thread(NamedThread thread) override
        {
            if (thread == NamedThread::GameThread)
            {
                game_wake_event_.trigger();
            }
            else if (thread == NamedThread::RenderingThread)
            {
                render_wake_event_.trigger();
            }
        }

        TaskGraphShutdownResult shutdown(TaskGraphShutdownMode mode) override
        {
            bool expected = false;
            if (!shutdown_started_.compare_exchange_strong(expected, true))
            {
                return shutdown_complete_.load()
                           ? TaskGraphShutdownResult{TaskGraphStatus::success()}
                           : TaskGraphShutdownResult{failure(TaskGraphErrorCode::InvalidState,
                                                             "Task Graph shutdown is already in progress")};
            }
            unpublish_active_task_graph(*this);
            accepting_tasks_.store(false);

            TaskGraphStatus shutdown_status = TaskGraphStatus::success();
            if (mode == TaskGraphShutdownMode::CancelPending)
            {
                cancel_pending_tasks();
            }
            else
            {
                shutdown_status = drain_tasks();
                if (!shutdown_status.succeeded())
                {
                    cancel_pending_tasks();
                }
            }

            wait_for_running_tasks();
            worker_stop_requested_.store(true);
            worker_queue_.stop();
            stop_and_join_workers();
            shutdown_complete_.store(true);
            clear_current_thread_binding();
            return {std::move(shutdown_status)};
        }

        ThreadStatus attach_worker(std::uint32_t)
        {
            if (tls_graph_ != nullptr)
            {
                return ThreadStatus::failure(ThreadErrorCode::InvalidState,
                                             "Task Graph worker thread already has a binding");
            }
            tls_graph_ = this;
            tls_thread_ = NamedThread::AnyWorker;
            return ThreadStatus::success();
        }

        std::uint32_t run_worker()
        {
            std::uint32_t high_priority_streak = 0;
            while (!worker_stop_requested_.load())
            {
                BaseGraphTask* task = nullptr;
                if (!worker_queue_.wait_dequeue(task, high_priority_streak))
                {
                    break;
                }
                execute_owned_task(task, NamedThread::AnyWorker);
            }
            return 0;
        }

        void wake_workers() { worker_queue_.wake_all(); }

        void clear_worker_binding()
        {
            if (tls_graph_ == this)
            {
                tls_current_task_ = nullptr;
                tls_thread_ = NamedThread::Unknown;
                tls_graph_ = nullptr;
            }
        }

      private:
        friend class TaskGraphWorkerRunnable;

        BaseGraphTask* accept_task(std::unique_ptr<BaseGraphTask> task) override
        {
            if (!task)
            {
                throw TaskGraphException(
                    failure(TaskGraphErrorCode::InvalidState, "Task Graph cannot accept a null task"));
            }
            if (!accepting_tasks_.load())
            {
                throw TaskGraphException(failure(TaskGraphErrorCode::Stopped, "Task Graph no longer accepts tasks"));
            }
            if (!target_is_available(task->get_desired_thread()))
            {
                throw TaskGraphException(
                    failure(TaskGraphErrorCode::TargetUnavailable, "GraphTask target Named Thread is not attached"));
            }

            std::unique_lock<std::mutex> lock(tasks_mutex_);
            if (!accepting_tasks_.load())
            {
                TaskGraphStatus status =
                    TaskGraphStatus::failure(TaskGraphErrorCode::Stopped, "Task Graph stopped while accepting a task");
                lock.unlock();
                report(status);
                throw TaskGraphException(std::move(status));
            }
            if (outstanding_tasks_ >= config_.max_tasks_in_flight)
            {
                TaskGraphStatus status =
                    TaskGraphStatus::failure(TaskGraphErrorCode::Overloaded, "Task Graph reached max_tasks_in_flight");
                lock.unlock();
                report(status);
                throw TaskGraphException(std::move(status));
            }

            BaseGraphTask* accepted = task.get();
            tasks_.emplace(accepted, std::move(task));
            ++outstanding_tasks_;
            return accepted;
        }

        void abandon_task(BaseGraphTask& task) noexcept override
        {
            std::unique_ptr<BaseGraphTask> abandoned;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                const auto found = tasks_.find(&task);
                if (found == tasks_.end())
                {
                    return;
                }
                abandoned = std::move(found->second);
                tasks_.erase(found);
                --outstanding_tasks_;
            }
            tasks_condition_.notify_all();
        }

        void queue_task(BaseGraphTask& task) override
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            if (tasks_.find(&task) == tasks_.end())
            {
                return;
            }

            const NamedThread target = map_target(task.get_desired_thread());
            bool enqueued = false;
            if (target == NamedThread::AnyWorker)
            {
                enqueued = worker_queue_.enqueue(&task, task.get_priority());
            }
            else if (target == NamedThread::GameThread)
            {
                enqueued = game_queue_.enqueue(&task);
                if (enqueued)
                {
                    game_ready_count_.fetch_add(1);
                    game_wake_event_.trigger();
                }
            }
            else if (target == NamedThread::RenderingThread)
            {
                enqueued = render_queue_.enqueue(&task);
                if (enqueued)
                {
                    render_ready_count_.fetch_add(1);
                    render_wake_event_.trigger();
                }
            }
            if (!enqueued)
            {
                TaskGraphStatus status = TaskGraphStatus::failure(TaskGraphErrorCode::Overloaded,
                                                                  "Task Graph ready queue rejected an accepted task");
                lock.unlock();
                report(status);
                throw TaskGraphException(std::move(status));
            }
        }

        NamedThread map_target(NamedThread desired_thread) const
        {
            if (!config_.multithreaded &&
                (desired_thread == NamedThread::AnyWorker || desired_thread == NamedThread::RenderingThread))
            {
                return NamedThread::GameThread;
            }
            return desired_thread;
        }

        bool target_is_available(NamedThread desired_thread) const
        {
            const NamedThread target = map_target(desired_thread);
            if (target == NamedThread::AnyWorker)
            {
                return worker_thread_count_ != 0;
            }

            std::lock_guard<std::mutex> lock(binding_mutex_);
            if (target == NamedThread::GameThread)
            {
                return game_thread_id_ != std::thread::id{};
            }
            if (target == NamedThread::RenderingThread)
            {
                return render_thread_id_ != std::thread::id{};
            }
            return false;
        }

        TaskGraphStatus validate_named_thread_caller(NamedThread current_thread) const
        {
            if ((current_thread != NamedThread::GameThread && current_thread != NamedThread::RenderingThread) ||
                tls_graph_ != this || tls_thread_ != current_thread)
            {
                return failure(TaskGraphErrorCode::InvalidCaller,
                               "Only the attached owner can pump a Named Thread queue");
            }
            return TaskGraphStatus::success();
        }

        bool process_one_named_task(NamedThread current_thread)
        {
            BaseGraphTask* task = nullptr;
            bool dequeued = false;
            if (current_thread == NamedThread::GameThread)
            {
                dequeued = game_queue_.dequeue(task);
                if (dequeued)
                {
                    game_ready_count_.fetch_sub(1);
                }
            }
            else if (current_thread == NamedThread::RenderingThread)
            {
                dequeued = render_queue_.dequeue(task);
                if (dequeued)
                {
                    render_ready_count_.fetch_sub(1);
                }
            }
            if (dequeued)
            {
                execute_owned_task(task, current_thread);
            }
            return dequeued;
        }

        bool help_one_task(NamedThread current_thread)
        {
            if (current_thread == NamedThread::GameThread)
            {
                if (process_one_named_task(NamedThread::GameThread))
                {
                    return true;
                }
                if (config_.multithreaded)
                {
                    return process_one_worker_task(NamedThread::GameThread);
                }
                return false;
            }
            if (current_thread == NamedThread::RenderingThread)
            {
                return process_one_named_task(NamedThread::RenderingThread);
            }
            if (current_thread == NamedThread::AnyWorker)
            {
                return process_one_worker_task(NamedThread::AnyWorker);
            }
            return false;
        }

        bool process_one_worker_task(NamedThread executing_thread)
        {
            BaseGraphTask* task = nullptr;
            std::uint32_t& high_priority_streak = tls_high_priority_streak_;
            if (!worker_queue_.try_dequeue(task, high_priority_streak))
            {
                return false;
            }
            execute_owned_task(task, executing_thread);
            return true;
        }

        void execute_owned_task(BaseGraphTask* task, NamedThread current_thread)
        {
            std::unique_ptr<BaseGraphTask> executing;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                const auto found = tasks_.find(task);
                if (found == tasks_.end())
                {
                    return;
                }
                executing = std::move(found->second);
                tasks_.erase(found);
                ++running_tasks_;
            }

            BaseGraphTask* previous_task = tls_current_task_;
            tls_current_task_ = executing.get();
            bool restore_game_processing = false;
            bool restore_render_processing = false;
            if (current_thread == NamedThread::GameThread)
            {
                restore_game_processing = !game_processing_.exchange(true);
            }
            else if (current_thread == NamedThread::RenderingThread)
            {
                restore_render_processing = !render_processing_.exchange(true);
            }
            if (current_thread == NamedThread::AnyWorker)
            {
                active_worker_tasks_.fetch_add(1);
            }
            try
            {
                executing->execute(current_thread);
            }
            catch (const std::exception& exception)
            {
                report(TaskGraphStatus::failure(TaskGraphErrorCode::InvalidState,
                                                std::string("Task Graph executor failure: ") + exception.what()));
            }
            catch (...)
            {
                report(
                    TaskGraphStatus::failure(TaskGraphErrorCode::InvalidState, "Unknown Task Graph executor failure"));
            }
            if (current_thread == NamedThread::AnyWorker)
            {
                active_worker_tasks_.fetch_sub(1);
            }
            if (restore_game_processing)
            {
                game_processing_.store(false);
            }
            if (restore_render_processing)
            {
                render_processing_.store(false);
            }
            tls_current_task_ = previous_task;
            executing.reset();

            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                --running_tasks_;
                --outstanding_tasks_;
            }
            tasks_condition_.notify_all();
        }

        void cancel_pending_tasks()
        {
            std::vector<std::unique_ptr<BaseGraphTask>> cancelled;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                cancelled.reserve(tasks_.size());
                for (auto& entry : tasks_)
                {
                    cancelled.push_back(std::move(entry.second));
                }
                outstanding_tasks_ -= cancelled.size();
                tasks_.clear();
            }

            for (const std::unique_ptr<BaseGraphTask>& task : cancelled)
            {
                try
                {
                    task->cancel();
                }
                catch (const std::exception& exception)
                {
                    report(TaskGraphStatus::failure(TaskGraphErrorCode::InvalidState,
                                                    std::string("Task cancellation callback failed: ") +
                                                        exception.what()));
                }
                catch (...)
                {
                    report(TaskGraphStatus::failure(TaskGraphErrorCode::InvalidState,
                                                    "Unknown Task cancellation callback failure"));
                }
            }
            tasks_condition_.notify_all();
        }

        TaskGraphStatus drain_tasks()
        {
            for (;;)
            {
                {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    if (outstanding_tasks_ == 0)
                    {
                        return TaskGraphStatus::success();
                    }
                }

                const NamedThread current_thread = get_current_thread_if_known();
                if (!help_one_task(current_thread))
                {
                    if (single_thread_cannot_make_progress(current_thread))
                    {
                        return failure(TaskGraphErrorCode::DeadlockRisk,
                                       "Single-thread Task Graph drain has no runnable work");
                    }
                    std::unique_lock<std::mutex> lock(tasks_mutex_);
                    tasks_condition_.wait_for(lock, wait_poll_interval);
                }
            }
        }

        bool single_thread_cannot_make_progress(NamedThread current_thread) const
        {
            if (config_.multithreaded || current_thread != NamedThread::GameThread)
            {
                return false;
            }
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            return outstanding_tasks_ != 0 && running_tasks_ == 0 && game_ready_count_.load() == 0;
        }

        void wait_for_running_tasks()
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_condition_.wait(lock, [this]() { return running_tasks_ == 0; });
        }

        void stop_and_join_workers()
        {
            for (const std::unique_ptr<RunnableThread>& worker : worker_threads_)
            {
                worker->request_stop();
            }
            worker_queue_.wake_all();
            for (const std::unique_ptr<RunnableThread>& worker : worker_threads_)
            {
                worker->wait_for_completion();
            }
            worker_threads_.clear();
        }

        void clear_current_thread_binding()
        {
            if (tls_graph_ == this)
            {
                tls_current_task_ = nullptr;
                tls_thread_ = NamedThread::Unknown;
                tls_high_priority_streak_ = 0;
                tls_graph_ = nullptr;
            }
        }

        TaskGraphStatus failure(TaskGraphErrorCode code, std::string message) const
        {
            TaskGraphStatus status = TaskGraphStatus::failure(code, std::move(message));
            report(status);
            return status;
        }

        void report(const TaskGraphStatus& status) const noexcept
        {
            try
            {
                if (diagnostics_)
                {
                    diagnostics_(status);
                }
            }
            catch (...)
            {
                // Diagnostics cannot change scheduler progress or shutdown behavior.
            }
        }

        TaskGraphConfig config_;
        ThreadManager& thread_manager_;
        TaskGraphDiagnostics diagnostics_;
        std::uint32_t worker_thread_count_ = 0;
        StallingTaskQueue worker_queue_;
        std::vector<std::unique_ptr<RunnableThread>> worker_threads_;
        std::atomic<bool> worker_stop_requested_{false};
        std::atomic<std::uint32_t> active_worker_tasks_{0};

        Queue<BaseGraphTask*, QueueMode::Mpsc> game_queue_;
        Queue<BaseGraphTask*, QueueMode::Mpsc> render_queue_;
        std::atomic<std::size_t> game_ready_count_{0};
        std::atomic<std::size_t> render_ready_count_{0};
        Event game_wake_event_;
        Event render_wake_event_;
        std::atomic<bool> game_return_requested_{false};
        std::atomic<bool> render_return_requested_{false};
        std::atomic<bool> game_processing_{false};
        std::atomic<bool> render_processing_{false};

        mutable std::mutex binding_mutex_;
        std::thread::id game_thread_id_;
        std::thread::id render_thread_id_;

        mutable std::mutex tasks_mutex_;
        std::condition_variable tasks_condition_;
        std::unordered_map<BaseGraphTask*, std::unique_ptr<BaseGraphTask>> tasks_;
        std::size_t outstanding_tasks_ = 0;
        std::size_t running_tasks_ = 0;
        std::atomic<bool> accepting_tasks_{true};
        std::atomic<bool> shutdown_started_{false};
        std::atomic<bool> shutdown_complete_{false};

        static thread_local TaskGraph* tls_graph_;
        static thread_local NamedThread tls_thread_;
        static thread_local BaseGraphTask* tls_current_task_;
        static thread_local std::uint32_t tls_high_priority_streak_;
    };

    thread_local TaskGraph* TaskGraph::tls_graph_ = nullptr;
    thread_local NamedThread TaskGraph::tls_thread_ = NamedThread::Unknown;
    thread_local BaseGraphTask* TaskGraph::tls_current_task_ = nullptr;
    thread_local std::uint32_t TaskGraph::tls_high_priority_streak_ = 0;

    ThreadStatus TaskGraphWorkerRunnable::init()
    {
        return task_graph_.attach_worker(worker_index_);
    }

    std::uint32_t TaskGraphWorkerRunnable::run()
    {
        return task_graph_.run_worker();
    }

    void TaskGraphWorkerRunnable::stop()
    {
        task_graph_.wake_workers();
    }

    void TaskGraphWorkerRunnable::exit()
    {
        task_graph_.clear_worker_binding();
    }

    TaskGraphCreateResult::TaskGraphCreateResult(TaskGraphStatus status, std::unique_ptr<TaskGraphInterface> task_graph)
        : status_(std::move(status)), task_graph_(std::move(task_graph))
    {
    }

    bool TaskGraphCreateResult::succeeded() const
    {
        return status_.succeeded() && task_graph_ != nullptr;
    }

    const TaskGraphStatus& TaskGraphCreateResult::status() const
    {
        return status_;
    }

    std::unique_ptr<TaskGraphInterface> TaskGraphCreateResult::take_task_graph()
    {
        return std::move(task_graph_);
    }

    TaskGraphCreateResult create_task_graph(TaskGraphConfig config, ThreadManager& thread_manager,
                                            TaskGraphDiagnostics diagnostics)
    {
        if (config.max_tasks_in_flight < 2)
        {
            return {TaskGraphStatus::failure(TaskGraphErrorCode::InvalidConfig,
                                             "Task Graph max_tasks_in_flight must be at least two"),
                    nullptr};
        }
        if (config.multithreaded && config.worker_thread_count > 1024)
        {
            return {TaskGraphStatus::failure(TaskGraphErrorCode::InvalidConfig,
                                             "Task Graph worker_thread_count exceeds the supported limit"),
                    nullptr};
        }
        if (!reserve_active_task_graph_creation())
        {
            TaskGraphStatus status = TaskGraphStatus::failure(TaskGraphErrorCode::InvalidState,
                                                              "A Task Graph instance is already active or starting");
            try
            {
                if (diagnostics)
                {
                    diagnostics(status);
                }
            }
            catch (...)
            {
                // Diagnostics cannot change active-instance publication.
            }
            return {std::move(status), nullptr};
        }

        std::unique_ptr<TaskGraph> task_graph;
        try
        {
            task_graph = std::make_unique<TaskGraph>(config, thread_manager, std::move(diagnostics));
            const TaskGraphStatus started = task_graph->start_workers();
            if (!started.succeeded())
            {
                task_graph->shutdown(TaskGraphShutdownMode::CancelPending);
                cancel_active_task_graph_creation();
                return {started, nullptr};
            }
        }
        catch (const std::exception& exception)
        {
            cancel_active_task_graph_creation();
            return {TaskGraphStatus::failure(TaskGraphErrorCode::InvalidConfig, exception.what()), nullptr};
        }
        publish_active_task_graph(*task_graph);
        return {TaskGraphStatus::success(), std::move(task_graph)};
    }

    bool TaskGraphInterface::is_running() noexcept
    {
        std::lock_guard<std::mutex> lock(active_task_graph_mutex);
        return active_task_graph != nullptr;
    }

    TaskGraphInterface& TaskGraphInterface::get()
    {
        std::lock_guard<std::mutex> lock(active_task_graph_mutex);
        if (active_task_graph == nullptr)
        {
            throw TaskGraphException(
                TaskGraphStatus::failure(TaskGraphErrorCode::Stopped, "No active Task Graph instance is running"));
        }
        return *active_task_graph;
    }
} // namespace toy3d
