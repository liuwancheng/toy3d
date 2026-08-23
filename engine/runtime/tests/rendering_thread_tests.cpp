#include "rendercore/rendering_thread.h"

#include "rendercore/render_command.h"
#include "task_graph/graph_task.h"
#include "task_graph/task_graph.h"
#include "threading/event.h"
#include "threading/thread.h"
#include "threading/thread_manager.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
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

    std::unique_ptr<toy3d::TaskGraphInterface> create_graph(
        toy3d::ThreadManager& thread_manager,
        bool multithreaded)
    {
        toy3d::TaskGraphCreateResult created = toy3d::create_task_graph(
            {multithreaded ? 1u : 0u, 256, multithreaded}, thread_manager);
        check(created.succeeded(), "RenderingThread fixture must create Task Graph");
        if (!created.succeeded())
        {
            return nullptr;
        }
        std::unique_ptr<toy3d::TaskGraphInterface> graph = created.take_task_graph();
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "RenderingThread fixture must attach GameThread");
        return graph;
    }

    void shutdown_graph(std::unique_ptr<toy3d::TaskGraphInterface>& graph)
    {
        if (graph)
        {
            check(graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
                "RenderingThread fixture Task Graph must shut down");
            graph.reset();
        }
    }

    void test_multi_thread_ready_pump_and_teardown()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph =
            create_graph(thread_manager, true);
        if (!graph)
        {
            return;
        }

        toy3d::RenderingThread rendering_thread(
            thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
        check(!rendering_thread.is_ready()
                && rendering_thread.get_thread_id() == std::thread::id{},
            "multi-thread controller must begin closed without an OS thread");

        bool early_enqueue_rejected = false;
        try
        {
            toy3d::dispatch_graph_task(
                *graph,
                "BeforeRenderingThreadReady",
                [](toy3d::NamedThread, const toy3d::GraphEventRef&) {},
                toy3d::NamedThread::RenderingThread);
        }
        catch (const toy3d::TaskGraphException& exception)
        {
            early_enqueue_rejected =
                exception.status().code == toy3d::TaskGraphErrorCode::TargetUnavailable;
        }
        check(early_enqueue_rejected,
            "render enqueue must be rejected before RenderingThread attachment");

        std::thread::id bootstrap_thread;
        toy3d::NamedThread bootstrap_named_thread = toy3d::NamedThread::Unknown;
        const toy3d::ThreadStatus started = rendering_thread.start(
            [&rendering_thread, &graph, &bootstrap_thread, &bootstrap_named_thread]()
            {
                bootstrap_thread = std::this_thread::get_id();
                bootstrap_named_thread = graph->get_current_thread_if_known();
                check(!rendering_thread.is_ready(),
                    "facade readiness must remain closed during bootstrap");
                return toy3d::ThreadStatus::success();
            });
        check(started.succeeded() && rendering_thread.is_ready(),
            "multi-thread start must publish ready after attach and bootstrap");
        check(rendering_thread.get_thread_id() != std::thread::id{}
                && rendering_thread.get_thread_id() == bootstrap_thread
                && bootstrap_named_thread == toy3d::NamedThread::RenderingThread,
            "bootstrap must run on the OS RenderingThread with named-thread TLS");

        std::vector<int> fifo;
        toy3d::GraphEventArray completions;
        for (int index = 0; index < 64; ++index)
        {
            completions.push_back(toy3d::dispatch_graph_task(
                *graph,
                "RenderingThreadWakeAndFifo",
                [&fifo, index](toy3d::NamedThread current, const toy3d::GraphEventRef&)
                {
                    check(current == toy3d::NamedThread::RenderingThread,
                        "named queue work must execute as logical RenderingThread");
                    fifo.push_back(index);
                },
                toy3d::NamedThread::RenderingThread));
        }
        check(graph->wait_until_tasks_complete(
                completions, toy3d::NamedThread::GameThread).succeeded(),
            "idle RenderingThread must wake and complete named queue work");
        bool fifo_preserved = fifo.size() == 64;
        for (int index = 0; fifo_preserved && index < 64; ++index)
        {
            fifo_preserved = fifo[index] == index;
        }
        check(fifo_preserved, "RenderingThread named queue must preserve producer FIFO");

        std::thread::id teardown_thread;
        const toy3d::ThreadStatus stopped = rendering_thread.stop(
            [&rendering_thread, &teardown_thread]()
            {
                teardown_thread = std::this_thread::get_id();
                check(rendering_thread.is_ready(),
                    "teardown must execute before facade readiness is withdrawn");
                return toy3d::ThreadStatus::success();
            });
        check(stopped.succeeded() && !rendering_thread.is_ready(),
            "multi-thread stop must teardown, request return, and join");
        check(teardown_thread == bootstrap_thread
                && rendering_thread.get_thread_id() == std::thread::id{},
            "teardown must run on logical RT before the OS thread is released");
        shutdown_graph(graph);
    }

    void test_single_thread_lifecycle()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph =
            create_graph(thread_manager, false);
        if (!graph)
        {
            return;
        }

        const std::thread::id game_thread = std::this_thread::get_id();
        toy3d::RenderingThread rendering_thread(
            thread_manager, *graph, toy3d::RenderingThreadMode::SingleThread);
        std::thread::id bootstrap_thread;
        check(rendering_thread.start([&graph, &bootstrap_thread]()
            {
                bootstrap_thread = std::this_thread::get_id();
                check(graph->get_current_thread_if_known()
                        == toy3d::NamedThread::GameThread,
                    "single-thread bootstrap must retain GameThread TLS");
                return toy3d::ThreadStatus::success();
            }).succeeded(),
            "single-thread start must initialize the logical RT inline");
        check(rendering_thread.is_ready()
                && rendering_thread.get_thread_id() == std::thread::id{}
                && bootstrap_thread == game_thread,
            "single-thread mode must not create an OS RenderingThread");

        std::thread::id teardown_thread;
        check(rendering_thread.stop([&teardown_thread]()
            {
                teardown_thread = std::this_thread::get_id();
                return toy3d::ThreadStatus::success();
            }).succeeded(),
            "single-thread stop must teardown inline");
        check(!rendering_thread.is_ready() && teardown_thread == game_thread,
            "single-thread lifecycle callbacks must stay on the GameThread");
        shutdown_graph(graph);
    }

    void test_start_failure_cleanup()
    {
        {
            toy3d::ThreadManager thread_manager;
            std::unique_ptr<toy3d::TaskGraphInterface> graph =
                create_graph(thread_manager, true);
            if (!graph)
            {
                return;
            }
            toy3d::RenderingThread rendering_thread(
                thread_manager,
                *graph,
                toy3d::RenderingThreadMode::MultiThread,
                [](std::function<void()>) -> std::unique_ptr<toy3d::Thread>
                {
                    throw std::runtime_error("injected thread creation failure");
                });
            const toy3d::ThreadStatus status = rendering_thread.start();
            check(status.code == toy3d::ThreadErrorCode::CreateFailed
                    && !rendering_thread.is_ready()
                    && rendering_thread.get_thread_id() == std::thread::id{},
                "thread creation failure must leave no ready or joinable state");
            shutdown_graph(graph);
        }

        {
            toy3d::ThreadManager thread_manager;
            std::unique_ptr<toy3d::TaskGraphInterface> graph =
                create_graph(thread_manager, true);
            if (!graph)
            {
                return;
            }
            toy3d::Event attached(toy3d::EventMode::ManualReset);
            std::thread existing_owner([&graph, &attached]()
            {
                graph->attach_to_thread(toy3d::NamedThread::RenderingThread);
                attached.trigger();
            });
            check(attached.wait_for(1s),
                "attach failure fixture must reserve RenderingThread binding");
            existing_owner.join();

            toy3d::RenderingThread rendering_thread(
                thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
            const toy3d::ThreadStatus status = rendering_thread.start();
            check(status.code == toy3d::ThreadErrorCode::InitFailed
                    && !rendering_thread.is_ready()
                    && rendering_thread.get_thread_id() == std::thread::id{},
                "attach failure must join the created thread and keep readiness closed");
            shutdown_graph(graph);
        }

        {
            toy3d::ThreadManager thread_manager;
            std::unique_ptr<toy3d::TaskGraphInterface> graph =
                create_graph(thread_manager, true);
            if (!graph)
            {
                return;
            }
            toy3d::RenderingThread rendering_thread(
                thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
            const toy3d::ThreadStatus status = rendering_thread.start([]()
            {
                return toy3d::ThreadStatus::failure(
                    toy3d::ThreadErrorCode::InitFailed,
                    "injected bootstrap failure");
            });
            check(status.code == toy3d::ThreadErrorCode::InitFailed
                    && !rendering_thread.is_ready()
                    && rendering_thread.get_thread_id() == std::thread::id{},
                "bootstrap failure must join the attached thread and keep readiness closed");
            shutdown_graph(graph);
        }
    }

    void test_repeated_start_stop()
    {
        for (int iteration = 0; iteration < 4; ++iteration)
        {
            for (const bool multithreaded : {true, false})
            {
                toy3d::ThreadManager thread_manager;
                std::unique_ptr<toy3d::TaskGraphInterface> graph =
                    create_graph(thread_manager, multithreaded);
                if (!graph)
                {
                    return;
                }
                toy3d::RenderingThread rendering_thread(
                    thread_manager,
                    *graph,
                    multithreaded
                        ? toy3d::RenderingThreadMode::MultiThread
                        : toy3d::RenderingThreadMode::SingleThread);
                check(rendering_thread.start().succeeded(),
                    "repeated lifecycle start must succeed");
                check(rendering_thread.stop().succeeded(),
                    "repeated lifecycle stop must succeed");
                shutdown_graph(graph);
            }
        }
    }

    void test_multi_thread_render_command_transport()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph =
            create_graph(thread_manager, true);
        if (!graph)
        {
            return;
        }

        auto move_only_contract = [payload = std::make_unique<int>(1)]() noexcept
        {
            static_cast<void>(payload);
        };
        static_assert(!std::is_copy_constructible<decltype(move_only_contract)>::value,
            "RenderCommand test payload must actually be move-only");
        static_assert(std::is_nothrow_invocable<decltype(move_only_contract)&>::value,
            "RenderCommand test callable must satisfy void() noexcept");

        toy3d::RenderingThread rendering_thread(
            thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
        check(rendering_thread.start().succeeded(),
            "multi-thread RenderCommand fixture must start RenderingThread");

        constexpr int command_count = 4096;
        std::vector<int> fifo;
        fifo.reserve(command_count);
        toy3d::Event fifo_complete(toy3d::EventMode::ManualReset);
        for (int index = 0; index < command_count; ++index)
        {
            toy3d::enqueue_render_command(
                "HighCountFifo", [&fifo, index]() noexcept
                {
                    fifo.push_back(index);
                });
        }
        toy3d::enqueue_render_command(
            "HighCountFifoComplete", [&fifo_complete]() noexcept
            {
                fifo_complete.trigger();
            });
        check(fifo_complete.wait_for(5s),
            "FireAndForget RenderCommands must wake RT and execute without completions");
        bool fifo_preserved = fifo.size() == command_count;
        for (int index = 0; fifo_preserved && index < command_count; ++index)
        {
            fifo_preserved = fifo[index] == index;
        }
        check(fifo_preserved,
            "high-count RenderCommands must preserve same-producer FIFO");

        std::vector<int> nested_order;
        toy3d::Event nested_complete(toy3d::EventMode::ManualReset);
        toy3d::enqueue_render_command(
            "OuterInlineCommand", [&nested_order, &nested_complete]() noexcept
            {
                nested_order.push_back(1);
                toy3d::enqueue_render_command(
                    "NestedInlineCommand", [&nested_order]() noexcept
                    {
                        nested_order.push_back(2);
                    });
                nested_order.push_back(3);
                nested_complete.trigger();
            });
        check(nested_complete.wait_for(2s),
            "logical RT must execute nested RenderCommand inline");
        check(nested_order == std::vector<int>({1, 2, 3}),
            "nested inline RenderCommand must not requeue or reorder work");

        std::thread::id disposal_thread;
        std::atomic<bool> move_only_executed{false};
        toy3d::Event disposal_complete(toy3d::EventMode::ManualReset);
        std::unique_ptr<int, std::function<void(int*)>> owned_payload(
            new int(7),
            [&disposal_thread](int* value)
            {
                disposal_thread = std::this_thread::get_id();
                delete value;
            });
        toy3d::enqueue_render_command(
            "MoveOnlyOwnership",
            [payload = std::move(owned_payload), &move_only_executed]() noexcept
            {
                move_only_executed.store(*payload == 7);
            });
        check(!owned_payload,
            "GT must relinquish move-only payload ownership exactly once");
        toy3d::enqueue_render_command(
            "ObservePayloadDisposal", [&disposal_complete]() noexcept
            {
                disposal_complete.trigger();
            });
        check(disposal_complete.wait_for(2s),
            "command after move-only payload must execute");
        check(move_only_executed.load()
                && disposal_thread == rendering_thread.get_thread_id(),
            "move-only command payload must execute and be destroyed on logical RT");

        toy3d::TaskGraphStatus worker_status;
        toy3d::GraphEventRef worker_complete = toy3d::dispatch_graph_task(
            *graph,
            "IllegalRenderCommandWorkerProducer",
            [&worker_status](toy3d::NamedThread, const toy3d::GraphEventRef&)
            {
                try
                {
                    toy3d::enqueue_render_command(
                        "WorkerMustFailFast", []() noexcept {});
                }
                catch (const toy3d::TaskGraphException& exception)
                {
                    worker_status = exception.status();
                }
            });
        check(graph->wait_until_task_completes(
                worker_complete, toy3d::NamedThread::GameThread).succeeded(),
            "worker producer fixture must complete");
        check(worker_status.code == toy3d::TaskGraphErrorCode::InvalidCaller
                && worker_status.message.find("AnyWorker") != std::string::npos
                && worker_status.message.find("WorkerMustFailFast") != std::string::npos,
            "AnyWorker rejection must diagnose producer thread and command name");

        check(rendering_thread.stop().succeeded(),
            "multi-thread RenderCommand fixture must stop cleanly");
        shutdown_graph(graph);
    }

    void test_single_thread_render_command_inline()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph =
            create_graph(thread_manager, false);
        if (!graph)
        {
            return;
        }

        toy3d::RenderingThread rendering_thread(
            thread_manager, *graph, toy3d::RenderingThreadMode::SingleThread);
        check(rendering_thread.start().succeeded(),
            "single-thread RenderCommand fixture must start");

        std::vector<int> order;
        toy3d::enqueue_render_command(
            "SingleThreadInline", [&order]() noexcept
            {
                order.push_back(1);
                toy3d::enqueue_render_command(
                    "SingleThreadNestedInline", [&order]() noexcept
                    {
                        order.push_back(2);
                    });
                order.push_back(3);
            });
        check(order == std::vector<int>({1, 2, 3}),
            "single-thread mode must run the same callable body inline on GT");

        check(rendering_thread.stop().succeeded(),
            "single-thread RenderCommand fixture must stop cleanly");
        shutdown_graph(graph);
    }
}

int main()
{
    test_multi_thread_ready_pump_and_teardown();
    test_single_thread_lifecycle();
    test_start_failure_cleanup();
    test_repeated_start_stop();
    test_multi_thread_render_command_transport();
    test_single_thread_render_command_inline();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " RenderingThread test(s) failed\n";
        return 1;
    }
    std::cout << "All RenderingThread tests passed\n";
    return 0;
}
