#include "rendercore/frame_synchronization.h"

#include "rendercore/render_command.h"
#include "rendercore/rendering_thread.h"
#include "threading/task_graph/task_graph.h"
#include "threading/event.h"
#include "threading/thread_manager.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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

    std::unique_ptr<toy3d::TaskGraphInterface> create_graph(toy3d::ThreadManager& thread_manager, bool multithreaded)
    {
        toy3d::TaskGraphCreateResult created =
            toy3d::create_task_graph({multithreaded ? 1u : 0u, 256, multithreaded}, thread_manager);
        check(created.succeeded(), "Frame synchronization fixture must create Task Graph");
        if (!created.succeeded())
        {
            return nullptr;
        }
        std::unique_ptr<toy3d::TaskGraphInterface> graph = created.take_task_graph();
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
              "Frame synchronization fixture must attach GameThread");
        return graph;
    }

    void shutdown_graph(std::unique_ptr<toy3d::TaskGraphInterface>& graph)
    {
        if (graph)
        {
            check(graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
                  "Frame synchronization fixture Task Graph must shut down");
            graph.reset();
        }
    }

    void test_fence_cpu_boundary_and_explicit_flush()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, true);
        if (!graph)
        {
            return;
        }
        toy3d::RenderingThread rendering_thread(thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
        check(rendering_thread.start().succeeded(), "Fence fixture must start RenderingThread");

        std::atomic<bool> business_submitted{false};
        std::atomic<bool> fake_gpu_completed{false};
        toy3d::enqueue_render_command("FakeQueueSubmit",
                                      [&business_submitted]() noexcept { business_submitted.store(true); });

        toy3d::RenderCommandFence fence;
        check(fence.begin_fence().succeeded(), "CPU fence must enqueue tracked work");
        const toy3d::RenderFenceWaitResult waited = fence.wait();
        check(waited.succeeded() && waited.rendering_thread_reached() && business_submitted.load() &&
                  !fake_gpu_completed.load(),
              "CPU fence must complete after prior RT work without waiting fake GPU completion");
        fake_gpu_completed.store(true);

        std::atomic<bool> flushed_command{false};
        toy3d::enqueue_render_command("ExplicitFlushWork",
                                      [&flushed_command]() noexcept { flushed_command.store(true); });
        const toy3d::RenderFenceWaitResult flushed = toy3d::flush_rendering_commands();
        check(flushed.succeeded() && flushed_command.load(), "explicit flush must wait for all prior RT CPU commands");

        toy3d::Event blocker_started(toy3d::EventMode::ManualReset);
        toy3d::Event release_blocker(toy3d::EventMode::ManualReset);
        toy3d::enqueue_render_command("NoImplicitFlush",
                                      [&blocker_started, &release_blocker]() noexcept
                                      {
                                          blocker_started.trigger();
                                          release_blocker.wait();
                                      });
        check(blocker_started.wait_for(2s), "ordinary command must reach RT independently of producer return");
        release_blocker.trigger();
        check(toy3d::flush_rendering_commands().succeeded(),
              "explicit flush must close the blocking command test boundary");

        check(rendering_thread.stop().succeeded(), "Fence fixture must stop RenderingThread");
        shutdown_graph(graph);
    }

    void test_frame_lag_modes()
    {
        {
            toy3d::ThreadManager thread_manager;
            std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, true);
            if (!graph)
            {
                return;
            }
            toy3d::RenderingThread rendering_thread(thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
            check(rendering_thread.start().succeeded(), "one-frame-lag fixture must start RenderingThread");

            toy3d::Event frame_zero_started(toy3d::EventMode::ManualReset);
            toy3d::Event release_frame_zero(toy3d::EventMode::ManualReset);
            toy3d::enqueue_render_command("BlockedFrameZero",
                                          [&frame_zero_started, &release_frame_zero]() noexcept
                                          {
                                              frame_zero_started.trigger();
                                              release_frame_zero.wait();
                                          });

            toy3d::FrameEndSync frame_sync(true);
            const toy3d::RenderFenceWaitResult frame_zero = frame_sync.sync_frame();
            check(frame_zero.succeeded() && frame_zero_started.wait_for(2s),
                  "default frame zero must wait the initially-complete previous fence");

            std::atomic<bool> second_sync_started{false};
            std::thread release_thread(
                [&second_sync_started, &release_frame_zero]()
                {
                    while (!second_sync_started.load())
                    {
                        std::this_thread::yield();
                    }
                    std::this_thread::sleep_for(30ms);
                    release_frame_zero.trigger();
                });
            second_sync_started.store(true);
            const auto wait_start = std::chrono::steady_clock::now();
            const toy3d::RenderFenceWaitResult frame_one = frame_sync.sync_frame();
            const auto wait_duration = std::chrono::steady_clock::now() - wait_start;
            release_thread.join();
            check(frame_one.succeeded() && wait_duration >= 20ms,
                  "default frame one must wait frame zero and permit at most one-frame lead");

            check(rendering_thread.stop().succeeded(), "one-frame-lag fixture must stop RenderingThread");
            shutdown_graph(graph);
        }

        {
            toy3d::ThreadManager thread_manager;
            std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, true);
            if (!graph)
            {
                return;
            }
            toy3d::RenderingThread rendering_thread(thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
            check(rendering_thread.start().succeeded(), "zero-lag fixture must start RenderingThread");

            toy3d::Event frame_started(toy3d::EventMode::ManualReset);
            toy3d::Event release_frame(toy3d::EventMode::ManualReset);
            toy3d::enqueue_render_command("BlockedZeroLagFrame",
                                          [&frame_started, &release_frame]() noexcept
                                          {
                                              frame_started.trigger();
                                              release_frame.wait();
                                          });
            check(frame_started.wait_for(2s), "zero-lag blocking command must start");

            std::thread release_thread(
                [&release_frame]()
                {
                    std::this_thread::sleep_for(30ms);
                    release_frame.trigger();
                });
            toy3d::FrameEndSync frame_sync(false);
            const auto wait_start = std::chrono::steady_clock::now();
            const toy3d::RenderFenceWaitResult frame = frame_sync.sync_frame();
            const auto wait_duration = std::chrono::steady_clock::now() - wait_start;
            release_thread.join();
            check(frame.succeeded() && wait_duration >= 20ms, "zero-lag mode must wait the current frame fence");

            check(rendering_thread.stop().succeeded(), "zero-lag fixture must stop RenderingThread");
            shutdown_graph(graph);
        }
    }

    void test_terminal_status_race()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, true);
        if (!graph)
        {
            return;
        }
        toy3d::RenderingThread rendering_thread(thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
        check(rendering_thread.start().succeeded(), "terminal race fixture must start RenderingThread");

        toy3d::Event blocker_started(toy3d::EventMode::ManualReset);
        toy3d::Event release_blocker(toy3d::EventMode::ManualReset);
        toy3d::enqueue_render_command("TerminalRaceBlocker",
                                      [&blocker_started, &release_blocker]() noexcept
                                      {
                                          blocker_started.trigger();
                                          release_blocker.wait();
                                      });

        toy3d::RenderCommandFence fence;
        check(fence.begin_fence().succeeded(), "terminal race Fence must enqueue after blocker");
        std::atomic<bool> wait_started{false};
        std::atomic<bool> terminal_latched{false};
        std::thread terminal_thread(
            [&blocker_started, &release_blocker, &wait_started, &terminal_latched]()
            {
                blocker_started.wait();
                while (!wait_started.load())
                {
                    std::this_thread::yield();
                }
                terminal_latched.store(true);
                release_blocker.trigger();
            });
        wait_started.store(true);
        const toy3d::RenderFenceWaitResult terminal_result = fence.wait(
            [&terminal_latched]()
            {
                return terminal_latched.load() ? toy3d::RenderFenceWaitResult::renderer_terminal("injected terminal")
                                               : toy3d::RenderFenceWaitResult::reached();
            });
        terminal_thread.join();
        check(terminal_result.rendering_thread_reached() && terminal_result.has_renderer_terminal() &&
                  terminal_result.renderer_error() == "injected terminal" && !terminal_result.succeeded(),
              "Fence waiter must wake and propagate an immutable terminal diagnostic");

        check(rendering_thread.stop().succeeded(), "terminal race fixture must stop RenderingThread");
        shutdown_graph(graph);
    }

    void test_framework_cancellation_wakes_waiter()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, true);
        if (!graph)
        {
            return;
        }
        toy3d::RenderingThread rendering_thread(thread_manager, *graph, toy3d::RenderingThreadMode::MultiThread);
        check(rendering_thread.start().succeeded(), "framework failure fixture must start RenderingThread");

        toy3d::Event blocker_started(toy3d::EventMode::ManualReset);
        toy3d::Event release_blocker(toy3d::EventMode::ManualReset);
        toy3d::enqueue_render_command("FrameworkFailureBlocker",
                                      [&blocker_started, &release_blocker]() noexcept
                                      {
                                          blocker_started.trigger();
                                          release_blocker.wait();
                                      });

        toy3d::RenderCommandFence fence;
        check(fence.begin_fence().succeeded(), "framework failure Fence must enqueue after blocker");
        std::atomic<bool> wait_started{false};
        toy3d::TaskGraphShutdownResult shutdown_result;
        std::thread shutdown_thread(
            [&graph, &blocker_started, &wait_started, &shutdown_result]()
            {
                blocker_started.wait();
                while (!wait_started.load())
                {
                    std::this_thread::yield();
                }
                shutdown_result = graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending);
            });
        wait_started.store(true);
        const toy3d::RenderFenceWaitResult cancelled = fence.wait();
        release_blocker.trigger();
        shutdown_thread.join();
        check(cancelled.framework_status().code == toy3d::TaskGraphErrorCode::Cancelled &&
                  !cancelled.rendering_thread_reached() && !cancelled.succeeded(),
              "Task Graph cancellation must wake Fence waiter with framework failure");
        check(shutdown_result.succeeded(), "injected framework cancellation must complete Task Graph shutdown");

        static_cast<void>(rendering_thread.stop());
        graph.reset();
    }
} // namespace

int main()
{
    test_fence_cpu_boundary_and_explicit_flush();
    test_frame_lag_modes();
    test_terminal_status_race();
    test_framework_cancellation_wakes_waiter();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " frame synchronization test(s) failed\n";
        return 1;
    }
    std::cout << "All frame synchronization tests passed\n";
    return 0;
}
