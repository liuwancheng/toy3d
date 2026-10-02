#include "threading/task_graph/base_graph_task.h"
#include "threading/task_graph/graph_task.h"
#include "threading/task_graph/task_graph_interface.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    class SingleThreadTaskGraph final : public toy3d::TaskGraphInterface
    {
      public:
        toy3d::NamedThread get_current_thread_if_known() const override
        {
            return toy3d::NamedThread::GameThread;
        }

        toy3d::NamedThread get_render_thread() const override
        {
            return toy3d::NamedThread::GameThread;
        }

        std::uint32_t get_num_worker_threads() const override
        {
            return 0;
        }

        bool is_thread_processing_tasks(toy3d::NamedThread) const override
        {
            return false;
        }

        toy3d::TaskGraphStatus attach_to_thread(toy3d::NamedThread) override
        {
            return toy3d::TaskGraphStatus::success();
        }

        std::uint64_t process_thread_until_idle(toy3d::NamedThread) override
        {
            return process_until_idle();
        }

        void process_thread_until_request_return(toy3d::NamedThread) override
        {
            process_until_idle();
        }

        void request_return(toy3d::NamedThread) override
        {
        }

        toy3d::TaskWaitResult wait_until_tasks_complete(const toy3d::GraphEventArray& tasks,
                                                        toy3d::NamedThread) override
        {
            while (!all_complete(tasks))
            {
                if (!process_one())
                {
                    throw std::logic_error("Single-thread graph cannot make progress");
                }
            }
            return {toy3d::TaskGraphStatus::success()};
        }

        void trigger_event_when_tasks_complete(toy3d::Event& event, const toy3d::GraphEventArray& tasks,
                                               toy3d::NamedThread) override
        {
            wait_until_tasks_complete(tasks, toy3d::NamedThread::GameThread);
            event.trigger();
        }

        void wake_named_thread(toy3d::NamedThread) override
        {
        }

        toy3d::TaskGraphShutdownResult shutdown(toy3d::TaskGraphShutdownMode) override
        {
            return {toy3d::TaskGraphStatus::success()};
        }

        std::uint64_t process_until_idle()
        {
            std::uint64_t processed = 0;
            while (process_one())
            {
                ++processed;
            }
            return processed;
        }

        std::size_t owned_task_count() const
        {
            return tasks_.size();
        }

      private:
        toy3d::BaseGraphTask* accept_task(std::unique_ptr<toy3d::BaseGraphTask> task) override
        {
            toy3d::BaseGraphTask* accepted = task.get();
            tasks_.push_back(std::move(task));
            return accepted;
        }

        void abandon_task(toy3d::BaseGraphTask& task) noexcept override
        {
            const auto iterator = std::find_if(tasks_.begin(), tasks_.end(),
                                               [&task](const std::unique_ptr<toy3d::BaseGraphTask>& owned)
                                               {
                                                   return owned.get() == &task;
                                               });
            if (iterator != tasks_.end())
            {
                tasks_.erase(iterator);
            }
        }

        void queue_task(toy3d::BaseGraphTask& task) override
        {
            ready_tasks_.push_back(&task);
        }

        static bool all_complete(const toy3d::GraphEventArray& tasks)
        {
            return std::all_of(tasks.begin(), tasks.end(),
                               [](const toy3d::GraphEventRef& task)
                               {
                                   return task && task->is_complete();
                               });
        }

        bool process_one()
        {
            if (ready_tasks_.empty())
            {
                return false;
            }

            toy3d::BaseGraphTask* next = ready_tasks_.front();
            ready_tasks_.pop_front();
            const auto iterator = std::find_if(tasks_.begin(), tasks_.end(),
                                               [next](const std::unique_ptr<toy3d::BaseGraphTask>& task)
                                               {
                                                   return task.get() == next;
                                               });
            if (iterator == tasks_.end())
            {
                throw std::logic_error("Ready task is not owned by the task graph");
            }

            std::unique_ptr<toy3d::BaseGraphTask> executing = std::move(*iterator);
            tasks_.erase(iterator);
            executing->execute(toy3d::NamedThread::GameThread);
            return true;
        }

        std::vector<std::unique_ptr<toy3d::BaseGraphTask>> tasks_;
        std::deque<toy3d::BaseGraphTask*> ready_tasks_;
    };

    struct RecordingState
    {
        std::vector<int> order;
        int constructed_payloads = 0;
        int executed_payloads = 0;
        int destroyed_payloads = 0;
        int live_payloads = 0;
    };

    class RecordingTask final
    {
      public:
        RecordingTask(std::shared_ptr<RecordingState> state, int id) : state_(std::move(state)), id_(id)
        {
            ++state_->constructed_payloads;
            ++state_->live_payloads;
        }

        ~RecordingTask()
        {
            ++state_->destroyed_payloads;
            --state_->live_payloads;
        }

        static toy3d::NamedThread get_desired_thread()
        {
            return toy3d::NamedThread::GameThread;
        }

        static toy3d::TaskPriority get_priority()
        {
            return toy3d::TaskPriority::Normal;
        }

        static toy3d::SubsequentsMode get_subsequents_mode()
        {
            return toy3d::SubsequentsMode::TrackSubsequents;
        }

        void do_task(toy3d::NamedThread current_thread, const toy3d::GraphEventRef&)
        {
            if (current_thread != toy3d::NamedThread::GameThread)
            {
                throw std::logic_error("RecordingTask ran on the wrong logical thread");
            }
            ++state_->executed_payloads;
            state_->order.push_back(id_);
        }

      private:
        std::shared_ptr<RecordingState> state_;
        int id_ = 0;
    };

    void test_diamond_dependencies_and_completed_prerequisite()
    {
        SingleThreadTaskGraph graph;
        auto state = std::make_shared<RecordingState>();
        toy3d::GraphEventRef root =
            toy3d::GraphTask<RecordingTask>::create_task(graph).construct_and_dispatch_when_ready(state, 1);
        toy3d::GraphEventArray root_dependency{root};
        toy3d::GraphEventRef left = toy3d::GraphTask<RecordingTask>::create_task(graph, &root_dependency)
                                        .construct_and_dispatch_when_ready(state, 2);
        toy3d::GraphEventRef right = toy3d::GraphTask<RecordingTask>::create_task(graph, &root_dependency)
                                         .construct_and_dispatch_when_ready(state, 3);
        toy3d::GraphEventArray join_dependencies{left, right};
        toy3d::GraphEventRef join = toy3d::GraphTask<RecordingTask>::create_task(graph, &join_dependencies)
                                        .construct_and_dispatch_when_ready(state, 4);

        check(!join->is_complete(), "a DAG must remain pending before its ready queue is pumped");
        join->wait(graph);
        check(state->order == std::vector<int>({1, 2, 3, 4}),
              "diamond dependencies must execute in prerequisite order and FIFO ready order");
        check(join->get_outcome() == toy3d::TaskOutcome::Succeeded,
              "a successful task must publish a successful completion outcome");
        check(state->live_payloads == 0 && graph.owned_task_count() == 0,
              "completed graph tasks must release their payload and graph ownership");
        check(state->constructed_payloads == 4 && state->executed_payloads == 4 && state->destroyed_payloads == 4,
              "each accepted TaskType payload must be constructed, executed, and destroyed once");

        toy3d::GraphEventArray completed_dependency{join};
        toy3d::GraphEventRef late = toy3d::GraphTask<RecordingTask>::create_task(graph, &completed_dependency)
                                        .construct_and_dispatch_when_ready(state, 5);
        check(graph.process_until_idle() == 1 && late->is_complete(),
              "a prerequisite completed before registration must be consumed immediately");
    }

    class DelayedCompletionTask final
    {
      public:
        DelayedCompletionTask(toy3d::GraphEventArray dependencies, std::shared_ptr<RecordingState> state, int id)
            : dependencies_(std::move(dependencies)), state_(std::move(state)), id_(id)
        {
        }

        static toy3d::NamedThread get_desired_thread()
        {
            return toy3d::NamedThread::GameThread;
        }

        static toy3d::TaskPriority get_priority()
        {
            return toy3d::TaskPriority::Normal;
        }

        static toy3d::SubsequentsMode get_subsequents_mode()
        {
            return toy3d::SubsequentsMode::TrackSubsequents;
        }

        void do_task(toy3d::NamedThread, const toy3d::GraphEventRef& completion_event)
        {
            state_->order.push_back(id_);
            for (const toy3d::GraphEventRef& dependency : dependencies_)
            {
                completion_event->dont_complete_until(dependency);
            }
        }

      private:
        toy3d::GraphEventArray dependencies_;
        std::shared_ptr<RecordingState> state_;
        int id_ = 0;
    };

    void test_dont_complete_until_delays_subsequents()
    {
        SingleThreadTaskGraph graph;
        auto state = std::make_shared<RecordingState>();
        toy3d::GraphTask<RecordingTask>* first_held_child =
            toy3d::GraphTask<RecordingTask>::create_task(graph).construct_and_hold(state, 2);
        toy3d::GraphEventRef first_child = first_held_child->get_completion_event();
        toy3d::GraphTask<RecordingTask>* second_held_child =
            toy3d::GraphTask<RecordingTask>::create_task(graph).construct_and_hold(state, 3);
        toy3d::GraphEventRef second_child = second_held_child->get_completion_event();
        toy3d::GraphEventArray delayed_dependencies{first_child, second_child};
        toy3d::GraphEventRef parent =
            toy3d::GraphTask<DelayedCompletionTask>::create_task(graph).construct_and_dispatch_when_ready(
                delayed_dependencies, state, 1);
        toy3d::GraphEventArray parent_dependency{parent};
        toy3d::GraphEventRef subsequent = toy3d::GraphTask<RecordingTask>::create_task(graph, &parent_dependency)
                                              .construct_and_dispatch_when_ready(state, 4);

        check(graph.process_until_idle() == 1 && !parent->is_complete(),
              "an owning task must remain incomplete while its delayed children are held");

        first_held_child->unlock();
        check(graph.process_until_idle() == 1 && !parent->is_complete(),
              "one incomplete child must keep the owning completion open");
        second_held_child->unlock();
        subsequent->wait(graph);
        check(state->order == std::vector<int>({1, 2, 3, 4}),
              "dont_complete_until must delay subsequents until every added event completes");
    }

    class ThrowingTask final
    {
      public:
        explicit ThrowingTask(std::shared_ptr<RecordingState> state) : state_(std::move(state))
        {
        }

        static toy3d::NamedThread get_desired_thread()
        {
            return toy3d::NamedThread::GameThread;
        }

        static toy3d::TaskPriority get_priority()
        {
            return toy3d::TaskPriority::High;
        }

        static toy3d::SubsequentsMode get_subsequents_mode()
        {
            return toy3d::SubsequentsMode::TrackSubsequents;
        }

        void do_task(toy3d::NamedThread, const toy3d::GraphEventRef&)
        {
            state_->order.push_back(1);
            throw std::runtime_error("expected task failure");
        }

      private:
        std::shared_ptr<RecordingState> state_;
    };

    void test_failure_hold_fire_and_forget_and_function_task()
    {
        SingleThreadTaskGraph graph;
        auto state = std::make_shared<RecordingState>();
        toy3d::GraphEventRef failed =
            toy3d::GraphTask<ThrowingTask>::create_task(graph).construct_and_dispatch_when_ready(state);
        toy3d::GraphEventArray failed_dependency{failed};
        toy3d::GraphTask<RecordingTask>* held =
            toy3d::GraphTask<RecordingTask>::create_task(graph, &failed_dependency).construct_and_hold(state, 2);
        toy3d::GraphEventRef held_completion = held->get_completion_event();

        check(graph.process_until_idle() == 1,
              "a held task must not become ready when only its prerequisites complete");
        check(failed->get_outcome() == toy3d::TaskOutcome::Failed,
              "task exceptions must be captured as a failed completion outcome");
        held->unlock();
        held_completion->wait(graph);
        check(state->order == std::vector<int>({1, 2}),
              "failure must not cancel a dependent and unlock must release the hold token");

        toy3d::GraphEventRef fire_and_forget = toy3d::dispatch_graph_task(
            graph, "FireAndForget",
            [state](toy3d::NamedThread, const toy3d::GraphEventRef& completion_event)
            {
                check(!completion_event, "a FireAndForget task must execute without a completion event");
                state->order.push_back(3);
            },
            toy3d::NamedThread::GameThread, nullptr, toy3d::SubsequentsMode::FireAndForget);
        check(!fire_and_forget, "FireAndForget dispatch must return an empty event handle");
        graph.process_until_idle();
        check(state->order == std::vector<int>({1, 2, 3}),
              "the function convenience must use the same graph execution path");
    }

    void test_invalid_completion_dependency_usage()
    {
        toy3d::GraphEventRef event = toy3d::GraphEvent::create_graph_event();
        bool rejected = false;
        try
        {
            event->dont_complete_until(toy3d::GraphEvent::create_graph_event());
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
        check(rejected, "dont_complete_until must reject calls outside the owning task");
    }
} // namespace

int main()
{
    test_diamond_dependencies_and_completed_prerequisite();
    test_dont_complete_until_delays_subsequents();
    test_failure_hold_fire_and_forget_and_function_task();
    test_invalid_completion_dependency_usage();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " task graph test(s) failed\n";
        return 1;
    }
    std::cout << "All task graph tests passed\n";
    return 0;
}
