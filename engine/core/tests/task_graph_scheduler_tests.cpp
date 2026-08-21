#include "task_graph/graph_task.h"
#include "task_graph/task_graph.h"
#include "threading/event.h"
#include "threading/thread_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    class SchedulerTask final
    {
    public:
        SchedulerTask(
            toy3d::GraphTaskFunction function,
            toy3d::NamedThread desired_thread,
            toy3d::TaskPriority priority = toy3d::TaskPriority::Normal,
            toy3d::SubsequentsMode mode = toy3d::SubsequentsMode::TrackSubsequents)
            : function_(std::move(function)),
              desired_thread_(desired_thread),
              priority_(priority),
              mode_(mode)
        {
        }

        toy3d::NamedThread get_desired_thread() const
        {
            return desired_thread_;
        }

        toy3d::TaskPriority get_priority() const
        {
            return priority_;
        }

        toy3d::SubsequentsMode get_subsequents_mode() const
        {
            return mode_;
        }

        void do_task(
            toy3d::NamedThread current_thread,
            const toy3d::GraphEventRef& completion_event)
        {
            function_(current_thread, completion_event);
        }

    private:
        toy3d::GraphTaskFunction function_;
        toy3d::NamedThread desired_thread_ = toy3d::NamedThread::AnyWorker;
        toy3d::TaskPriority priority_ = toy3d::TaskPriority::Normal;
        toy3d::SubsequentsMode mode_ = toy3d::SubsequentsMode::TrackSubsequents;
    };

    std::unique_ptr<toy3d::TaskGraphInterface> create_graph(
        toy3d::ThreadManager& thread_manager,
        toy3d::TaskGraphConfig config,
        toy3d::TaskGraphDiagnostics diagnostics = {})
    {
        toy3d::TaskGraphCreateResult created = toy3d::create_task_graph(
            config, thread_manager, std::move(diagnostics));
        check(created.succeeded(), "Task Graph factory must create the requested scheduler");
        return created.succeeded() ? created.take_task_graph() : nullptr;
    }

    void test_single_thread_fifo_drain_and_self_wait()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(
            thread_manager, {0, 16, false});
        if (!graph)
        {
            return;
        }

        check(graph->get_render_thread() == toy3d::NamedThread::GameThread
                && graph->get_num_worker_threads() == 0,
            "single-thread mode must map logical rendering and worker work to GameThread");
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "GameThread must attach to a new single-thread graph");
        check(graph->get_current_thread_if_known() == toy3d::NamedThread::GameThread,
            "attached GameThread identity must be queryable");

        std::vector<int> order;
        std::vector<toy3d::GraphEventRef> events;
        const toy3d::NamedThread targets[] = {
            toy3d::NamedThread::AnyWorker,
            toy3d::NamedThread::RenderingThread,
            toy3d::NamedThread::GameThread};
        for (int index = 0; index < 3; ++index)
        {
            events.push_back(toy3d::dispatch_graph_task(
                *graph,
                "SingleThreadFifo",
                [&order, index](toy3d::NamedThread current_thread, const toy3d::GraphEventRef&)
                {
                    if (current_thread == toy3d::NamedThread::GameThread)
                    {
                        order.push_back(index + 1);
                    }
                },
                targets[index]));
        }

        std::atomic<toy3d::TaskGraphErrorCode> self_wait_result{
            toy3d::TaskGraphErrorCode::None};
        toy3d::GraphEventRef self_wait = toy3d::dispatch_graph_task(
            *graph,
            "SelfWait",
            [&graph, &self_wait_result](
                toy3d::NamedThread,
                const toy3d::GraphEventRef& completion_event)
            {
                self_wait_result.store(
                    graph->wait_until_task_completes(completion_event).status.code);
            },
            toy3d::NamedThread::GameThread);
        events.push_back(self_wait);

        check(graph->shutdown(toy3d::TaskGraphShutdownMode::Drain).succeeded(),
            "Drain must pump the attached GameThread until accepted tasks finish");
        check(order == std::vector<int>({1, 2, 3}),
            "single-thread fallback must preserve one deterministic FIFO");
        check(self_wait_result.load() == toy3d::TaskGraphErrorCode::DeadlockRisk,
            "a running GraphTask must diagnose waiting for its own completion");
        check(std::all_of(events.begin(), events.end(), [](const toy3d::GraphEventRef& event)
            {
                return event->get_outcome() == toy3d::TaskOutcome::Succeeded;
            }),
            "Drain must publish successful completion for every executed task");
        check(graph->get_current_thread_if_known() == toy3d::NamedThread::Unknown,
            "shutdown must clear the caller's Named Thread binding");
    }

    void test_target_validation_saturation_and_cancel()
    {
        std::atomic<toy3d::TaskGraphErrorCode> diagnosed{
            toy3d::TaskGraphErrorCode::None};
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(
            thread_manager,
            {0, 2, false},
            [&diagnosed](const toy3d::TaskGraphStatus& status)
            {
                diagnosed.store(status.code);
            });
        if (!graph)
        {
            return;
        }

        bool unavailable_rejected = false;
        try
        {
            toy3d::dispatch_graph_task(
                *graph, "Unavailable", [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                toy3d::NamedThread::GameThread);
        }
        catch (const toy3d::TaskGraphException& exception)
        {
            unavailable_rejected =
                exception.status().code == toy3d::TaskGraphErrorCode::TargetUnavailable;
        }
        check(unavailable_rejected,
            "dispatch to an unattached Named Thread must return TargetUnavailable");

        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "GameThread attachment must succeed after target validation");
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).code
                == toy3d::TaskGraphErrorCode::InvalidState,
            "repeated Named Thread attachment must be rejected");
        check(diagnosed.load() == toy3d::TaskGraphErrorCode::InvalidState,
            "the diagnostics sink must observe repeated attachment errors");

        toy3d::GraphTask<SchedulerTask>* first =
            toy3d::GraphTask<SchedulerTask>::create_task(*graph)
                .construct_and_hold(
                    [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                    toy3d::NamedThread::GameThread);
        toy3d::GraphEventRef first_event = first->get_completion_event();
        toy3d::GraphTask<SchedulerTask>* second =
            toy3d::GraphTask<SchedulerTask>::create_task(*graph)
                .construct_and_hold(
                    [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                    toy3d::NamedThread::GameThread);
        toy3d::GraphEventRef second_event = second->get_completion_event();

        bool overloaded_rejected = false;
        try
        {
            toy3d::dispatch_graph_task(
                *graph, "Overloaded", [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                toy3d::NamedThread::GameThread);
        }
        catch (const toy3d::TaskGraphException& exception)
        {
            overloaded_rejected =
                exception.status().code == toy3d::TaskGraphErrorCode::Overloaded;
        }
        check(overloaded_rejected,
            "max_tasks_in_flight must reject excess work without inline execution");

        check(graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
            "CancelPending must close held tasks without executing them");
        check(first_event->get_outcome() == toy3d::TaskOutcome::Cancelled
                && second_event->get_outcome() == toy3d::TaskOutcome::Cancelled,
            "CancelPending must publish Cancelled for every pending completion");

        bool stopped_rejected = false;
        try
        {
            toy3d::dispatch_graph_task(
                *graph, "Stopped", [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                toy3d::NamedThread::GameThread);
        }
        catch (const toy3d::TaskGraphException& exception)
        {
            stopped_rejected =
                exception.status().code == toy3d::TaskGraphErrorCode::Stopped;
        }
        check(stopped_rejected, "dispatch after shutdown must return Stopped");
    }

    void test_single_thread_dependency_cycle_diagnostic()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(
            thread_manager, {0, 8, false});
        if (!graph)
        {
            return;
        }
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "cycle diagnostic graph must attach GameThread");

        toy3d::GraphEventRef delayed_until;
        toy3d::GraphTask<SchedulerTask>* first =
            toy3d::GraphTask<SchedulerTask>::create_task(*graph)
                .construct_and_hold(
                    [&delayed_until](
                        toy3d::NamedThread,
                        const toy3d::GraphEventRef& completion_event)
                    {
                        completion_event->dont_complete_until(delayed_until);
                    },
                    toy3d::NamedThread::GameThread);
        toy3d::GraphEventRef first_event = first->get_completion_event();
        toy3d::GraphEventArray first_dependency{first_event};
        toy3d::GraphEventRef second_event = toy3d::dispatch_graph_task(
            *graph,
            "CycleDependent",
            [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
            toy3d::NamedThread::GameThread,
            &first_dependency);
        delayed_until = second_event;
        first->unlock();

        check(graph->process_thread_until_idle(toy3d::NamedThread::GameThread) == 1,
            "the first cycle task must run before its delayed completion stalls");
        check(graph->wait_until_task_completes(second_event).status.code
                == toy3d::TaskGraphErrorCode::DeadlockRisk,
            "a single-thread dependency cycle with no runnable work must return DeadlockRisk");
        check(graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
            "CancelPending must safely break a diagnosed dependency cycle");
        check(first_event->get_outcome() == toy3d::TaskOutcome::Succeeded
                && second_event->get_outcome() == toy3d::TaskOutcome::Cancelled,
            "cycle teardown must close the pending task and release delayed completion");
    }

    void test_worker_priority_fairness_and_external_wait()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(
            thread_manager, {1, 64, true});
        if (!graph)
        {
            return;
        }
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "GameThread must attach before worker scheduling tests");

        toy3d::GraphTask<SchedulerTask>* gate =
            toy3d::GraphTask<SchedulerTask>::create_task(*graph)
                .construct_and_hold(
                    [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                    toy3d::NamedThread::AnyWorker);
        toy3d::GraphEventRef gate_event = gate->get_completion_event();
        toy3d::GraphEventArray gate_dependency{gate_event};

        std::mutex order_mutex;
        std::vector<char> order;
        toy3d::GraphEventArray scheduled;
        for (int index = 0; index < 16; ++index)
        {
            scheduled.push_back(
                toy3d::GraphTask<SchedulerTask>::create_task(*graph, &gate_dependency)
                    .construct_and_dispatch_when_ready(
                        [&order_mutex, &order](
                            toy3d::NamedThread, const toy3d::GraphEventRef&)
                        {
                            std::lock_guard<std::mutex> lock(order_mutex);
                            order.push_back('H');
                        },
                        toy3d::NamedThread::AnyWorker,
                        toy3d::TaskPriority::High));
        }
        for (int index = 0; index < 2; ++index)
        {
            scheduled.push_back(
                toy3d::GraphTask<SchedulerTask>::create_task(*graph, &gate_dependency)
                    .construct_and_dispatch_when_ready(
                        [&order_mutex, &order](
                            toy3d::NamedThread, const toy3d::GraphEventRef&)
                        {
                            std::lock_guard<std::mutex> lock(order_mutex);
                            order.push_back('N');
                        },
                        toy3d::NamedThread::AnyWorker,
                        toy3d::TaskPriority::Normal));
        }

        toy3d::Event all_scheduled(toy3d::EventMode::ManualReset);
        graph->trigger_event_when_tasks_complete(all_scheduled, scheduled);
        gate->unlock();
        check(all_scheduled.wait_for(2s),
            "the worker must complete every priority test task without a lost wakeup");
        check(order.size() == scheduled.size(),
            "the worker must execute every accepted High and Normal task exactly once");
        const auto first_normal = std::find(order.begin(), order.end(), 'N');
        check(first_normal != order.end()
                && std::distance(order.begin(), first_normal) <= 8,
            "continuous High work must yield to Normal work after the fairness limit");

        std::atomic<toy3d::TaskGraphErrorCode> worker_child_wait{
            toy3d::TaskGraphErrorCode::InvalidState};
        std::atomic<toy3d::TaskGraphErrorCode> worker_self_wait{
            toy3d::TaskGraphErrorCode::InvalidState};
        toy3d::Event nested_wait_done(toy3d::EventMode::ManualReset);
        toy3d::GraphEventRef nested_parent = toy3d::dispatch_graph_task(
            *graph,
            "WorkerNestedWait",
            [&graph, &worker_child_wait, &worker_self_wait, &nested_wait_done](
                toy3d::NamedThread,
                const toy3d::GraphEventRef& parent_completion)
            {
                toy3d::GraphEventRef child = toy3d::dispatch_graph_task(
                    *graph,
                    "WorkerNestedChild",
                    [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                    toy3d::NamedThread::AnyWorker);
                worker_child_wait.store(
                    graph->wait_until_task_completes(child).status.code);
                worker_self_wait.store(
                    graph->wait_until_task_completes(parent_completion).status.code);
                nested_wait_done.trigger();
            },
            toy3d::NamedThread::AnyWorker);
        check(nested_wait_done.wait_for(1s),
            "a worker waiting on worker work must help the only worker queue make progress");
        check(worker_child_wait.load() == toy3d::TaskGraphErrorCode::None
                && worker_self_wait.load() == toy3d::TaskGraphErrorCode::DeadlockRisk,
            "worker helping must restore the parent task identity for later self-wait checks");
        check(graph->wait_until_task_completes(nested_parent).succeeded(),
            "the nested worker parent must publish completion after helping its child");

        toy3d::GraphEventRef failed = toy3d::dispatch_graph_task(
            *graph,
            "ExternalFailureWait",
            [](toy3d::NamedThread, const toy3d::GraphEventRef&)
            {
                throw std::runtime_error("expected scheduler test failure");
            },
            toy3d::NamedThread::AnyWorker);
        std::atomic<toy3d::TaskGraphErrorCode> external_wait_result{
            toy3d::TaskGraphErrorCode::None};
        std::thread external_waiter([&graph, &failed, &external_wait_result]()
        {
            external_wait_result.store(
                graph->wait_until_task_completes(failed).status.code);
        });
        external_waiter.join();
        check(external_wait_result.load() == toy3d::TaskGraphErrorCode::TaskFailed,
            "an Unknown external thread must block and observe TaskFailed completion");
        check(graph->shutdown(toy3d::TaskGraphShutdownMode::Drain).succeeded(),
            "a worker graph must drain and join its RunnableThread workers");
    }

    void test_named_thread_routing_and_wrong_caller()
    {
        std::atomic<toy3d::TaskGraphErrorCode> diagnosed{
            toy3d::TaskGraphErrorCode::None};
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(
            thread_manager,
            {1, 32, true},
            [&diagnosed](const toy3d::TaskGraphStatus& status)
            {
                diagnosed.store(status.code);
            });
        if (!graph)
        {
            return;
        }
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "GameThread attachment must succeed for routing integration");

        std::thread wrong_caller([&graph]()
        {
            graph->process_thread_until_idle(toy3d::NamedThread::GameThread);
        });
        wrong_caller.join();
        check(diagnosed.load() == toy3d::TaskGraphErrorCode::InvalidCaller,
            "a non-owner thread must not pump the GameThread queue");

        toy3d::Event render_ready(toy3d::EventMode::ManualReset);
        std::atomic<toy3d::TaskGraphErrorCode> render_attach{
            toy3d::TaskGraphErrorCode::InvalidState};
        std::thread render_thread([&graph, &render_ready, &render_attach]()
        {
            render_attach.store(
                graph->attach_to_thread(
                    toy3d::NamedThread::RenderingThread).code);
            render_ready.trigger();
            if (render_attach.load() == toy3d::TaskGraphErrorCode::None)
            {
                graph->process_thread_until_request_return(
                    toy3d::NamedThread::RenderingThread);
            }
        });
        check(render_ready.wait_for(1s)
                && render_attach.load() == toy3d::TaskGraphErrorCode::None,
            "RenderingThread must attach on its external owner thread");

        std::atomic<toy3d::NamedThread> worker_current{toy3d::NamedThread::Unknown};
        std::atomic<toy3d::NamedThread> game_current{toy3d::NamedThread::Unknown};
        std::atomic<toy3d::NamedThread> render_current{toy3d::NamedThread::Unknown};
        std::atomic<bool> worker_reported_processing{false};
        std::atomic<bool> game_reported_processing{false};
        std::atomic<bool> render_reported_processing{false};
        toy3d::Event worker_done(toy3d::EventMode::ManualReset);
        toy3d::Event render_done(toy3d::EventMode::ManualReset);
        toy3d::GraphEventRef worker_event = toy3d::dispatch_graph_task(
            *graph,
            "WorkerRoute",
            [&graph, &worker_current, &worker_done, &worker_reported_processing](
                toy3d::NamedThread current, const toy3d::GraphEventRef&)
            {
                worker_current.store(current);
                worker_reported_processing.store(
                    graph->is_thread_processing_tasks(toy3d::NamedThread::AnyWorker));
                worker_done.trigger();
            },
            toy3d::NamedThread::AnyWorker);
        toy3d::GraphEventRef render_event = toy3d::dispatch_graph_task(
            *graph,
            "RenderRoute",
            [&graph, &render_current, &render_done, &render_reported_processing](
                toy3d::NamedThread current, const toy3d::GraphEventRef&)
            {
                render_current.store(current);
                render_reported_processing.store(graph->is_thread_processing_tasks(
                    toy3d::NamedThread::RenderingThread));
                render_done.trigger();
            },
            toy3d::NamedThread::RenderingThread);
        toy3d::GraphEventRef game_event = toy3d::dispatch_graph_task(
            *graph,
            "GameRoute",
            [&graph, &game_current, &game_reported_processing](
                toy3d::NamedThread current, const toy3d::GraphEventRef&)
            {
                game_current.store(current);
                game_reported_processing.store(
                    graph->is_thread_processing_tasks(toy3d::NamedThread::GameThread));
            },
            toy3d::NamedThread::GameThread);

        check(worker_done.wait_for(1s), "AnyWorker task must execute on a worker RunnableThread");
        check(render_done.wait_for(1s),
            "render task must wake and execute on RenderingThread");
        check(graph->process_thread_until_idle(toy3d::NamedThread::GameThread) == 1,
            "GameThread owner must pump its own pending task");
        check(worker_current.load() == toy3d::NamedThread::AnyWorker
                && render_current.load() == toy3d::NamedThread::RenderingThread
                && game_current.load() == toy3d::NamedThread::GameThread,
            "each routing target must receive its exact logical execution identity");
        check(worker_reported_processing.load()
                && render_reported_processing.load()
                && game_reported_processing.load(),
            "processing-state queries must be true while each logical executor runs a task");
        check(graph->wait_until_tasks_complete(
                {worker_event, render_event, game_event}).succeeded(),
            "routing integration completions must all be observable from GameThread");

        graph->request_return(toy3d::NamedThread::RenderingThread);
        render_thread.join();
        check(graph->shutdown(toy3d::TaskGraphShutdownMode::Drain).succeeded(),
            "routing graph must drain after the external render loop returns");
    }

    void test_cancelled_prerequisites()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(
            thread_manager, {0, 16, false});
        if (!graph)
        {
            return;
        }
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "cancellation race graph must attach GameThread");

        toy3d::GraphTask<SchedulerTask>* prerequisite =
            toy3d::GraphTask<SchedulerTask>::create_task(*graph)
                .construct_and_hold(
                    [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                    toy3d::NamedThread::GameThread);
        toy3d::GraphEventRef prerequisite_event = prerequisite->get_completion_event();
        toy3d::GraphEventArray prerequisite_array{prerequisite_event};
        toy3d::GraphEventArray dependents;
        for (int index = 0; index < 8; ++index)
        {
            dependents.push_back(toy3d::dispatch_graph_task(
                *graph,
                "CancelledDependent",
                [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                toy3d::NamedThread::GameThread,
                &prerequisite_array));
        }

        check(graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
            "CancelPending must tolerate prerequisite callbacks racing task destruction");
        check(prerequisite_event->get_outcome() == toy3d::TaskOutcome::Cancelled
                && std::all_of(
                    dependents.begin(), dependents.end(),
                    [](const toy3d::GraphEventRef& event)
                    {
                        return event->get_outcome() == toy3d::TaskOutcome::Cancelled;
                    }),
            "cancelling a prerequisite DAG must close every accepted completion once");
    }

    void test_repeated_lifecycle()
    {
        toy3d::ThreadManager thread_manager;
        std::atomic<int> executed{0};
        constexpr int lifecycle_count = 64;
        constexpr int tasks_per_lifecycle = 24;
        for (int lifecycle = 0; lifecycle < lifecycle_count; ++lifecycle)
        {
            std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(
                thread_manager, {1, 64, true});
            if (!graph)
            {
                return;
            }
            check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
                "each recreated Task Graph must establish a fresh GameThread binding");
            for (int task = 0; task < tasks_per_lifecycle; ++task)
            {
                toy3d::dispatch_graph_task(
                    *graph,
                    "RepeatedLifecycle",
                    [&executed](toy3d::NamedThread, const toy3d::GraphEventRef&)
                    {
                        executed.fetch_add(1);
                    },
                    toy3d::NamedThread::AnyWorker);
            }
            check(graph->shutdown(toy3d::TaskGraphShutdownMode::Drain).succeeded(),
                "recreated Task Graph workers must drain and join cleanly");
        }
        check(executed.load() == lifecycle_count * tasks_per_lifecycle,
            "repeated create/shutdown must neither lose nor duplicate worker tasks");
    }
}

int main(int argument_count, char** arguments)
{
    const std::string selected = argument_count > 1 ? arguments[1] : "all";
    if (selected == "all" || selected == "single")
    {
        test_single_thread_fifo_drain_and_self_wait();
    }
    if (selected == "all" || selected == "validation")
    {
        test_target_validation_saturation_and_cancel();
    }
    if (selected == "all" || selected == "cycle")
    {
        test_single_thread_dependency_cycle_diagnostic();
    }
    if (selected == "all" || selected == "worker")
    {
        test_worker_priority_fairness_and_external_wait();
    }
    if (selected == "all" || selected == "routing")
    {
        test_named_thread_routing_and_wrong_caller();
    }
    if (selected == "all" || selected == "cancel")
    {
        test_cancelled_prerequisites();
    }
    if (selected == "all" || selected == "lifecycle")
    {
        test_repeated_lifecycle();
    }

    if (failure_count != 0)
    {
        std::cerr << failure_count << " Task Graph scheduler test(s) failed\n";
        return 1;
    }
    std::cout << "All Task Graph scheduler tests passed\n";
    return 0;
}
