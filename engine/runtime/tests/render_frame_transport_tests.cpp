#include "renderscene/render_frame_queue.h"
#include "renderscene/render_frame_dispatcher.h"

#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::RenderFramePacket make_packet(
        toy3d::RenderFrameId frame_id,
        const toy3d::RenderFrameCompletionRef& completion)
    {
        toy3d::RenderFramePacket packet;
        packet.frame_id = frame_id;
        packet.timing.delta_seconds = 1.0 / 60.0;
        packet.timing.total_seconds = 1.0;
        packet.completion = completion;
        return packet;
    }

    struct FakeProcessorState
    {
        std::mutex mutex;
        std::vector<toy3d::RenderFrameId> processed_frames;
        std::thread::id initialize_thread;
        std::thread::id process_thread;
        std::thread::id flush_thread;
        std::thread::id shutdown_thread;
        int initialize_count = 0;
        int flush_count = 0;
        int shutdown_count = 0;
        toy3d::RenderFrameId failing_frame;
        toy3d::RenderFrameId fatal_frame;
    };

    class FakeRenderFrameProcessor final : public toy3d::RenderFrameProcessor
    {
    public:
        explicit FakeRenderFrameProcessor(std::shared_ptr<FakeProcessorState> state)
            : state_(std::move(state))
        {
        }

        toy3d::RenderFrameExecutionStatus initialize() override
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            ++state_->initialize_count;
            state_->initialize_thread = std::this_thread::get_id();
            return toy3d::RenderFrameExecutionStatus::success();
        }

        toy3d::RenderFrameExecutionStatus process_frame(
            const toy3d::RenderFramePacket& packet) override
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->processed_frames.push_back(packet.frame_id);
            state_->process_thread = std::this_thread::get_id();
            if (packet.frame_id == state_->failing_frame)
            {
                return toy3d::RenderFrameExecutionStatus::frame_failure("fake frame failure");
            }
            if (packet.frame_id == state_->fatal_frame)
            {
                return toy3d::RenderFrameExecutionStatus::fatal_failure("fake fatal renderer failure");
            }
            return toy3d::RenderFrameExecutionStatus::success();
        }

        toy3d::RenderFrameExecutionStatus flush() override
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            ++state_->flush_count;
            state_->flush_thread = std::this_thread::get_id();
            return toy3d::RenderFrameExecutionStatus::success();
        }

        toy3d::RenderFrameExecutionStatus shutdown() override
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            ++state_->shutdown_count;
            state_->shutdown_thread = std::this_thread::get_id();
            return toy3d::RenderFrameExecutionStatus::success();
        }

    private:
        std::shared_ptr<FakeProcessorState> state_;
    };
}

int main()
{
    using namespace toy3d;
    using namespace std::chrono_literals;

    static_assert(!std::is_same<RenderFrameId, RenderSceneId>::value,
        "Frame and Scene identities must not be interchangeable");

    auto success_completion = std::make_shared<RenderFrameCompletion>();
    check(!success_completion->is_complete(),
        "A new completion must be pending");
    check(success_completion->complete_success(),
        "The first completion result must be accepted");
    check(!success_completion->complete_failure("late failure"),
        "A completion result must be immutable after the first signal");
    const RenderFrameCompletionResult success_result = success_completion->wait();
    check(success_result.state == RenderFrameCompletionState::Succeeded &&
        static_cast<bool>(success_result) && success_result.message.empty(),
        "A successful completion must wake waiters with an empty diagnostic");

    auto failure_completion = std::make_shared<RenderFrameCompletion>();
    auto failure_waiter = std::async(std::launch::async, [failure_completion]()
    {
        return failure_completion->wait();
    });
    check(failure_waiter.wait_for(20ms) == std::future_status::timeout,
        "wait() must block while a completion is pending");
    check(failure_completion->complete_failure("render failed"),
        "A pending completion must accept a failure result");
    check(failure_waiter.wait_for(1s) == std::future_status::ready,
        "Completing a frame must wake a blocked waiter");
    const RenderFrameCompletionResult failure_result = failure_waiter.get();
    check(failure_result.state == RenderFrameCompletionState::Failed &&
        !static_cast<bool>(failure_result) &&
        failure_result.message == "render failed",
        "A failed completion must preserve its diagnostic");

    RenderFrameQueue queue;
    const RenderFrameId first_frame = allocate_render_id<RenderFrameId>();
    const RenderFrameId second_frame = allocate_render_id<RenderFrameId>();
    auto first_completion = std::make_shared<RenderFrameCompletion>();
    auto second_completion = std::make_shared<RenderFrameCompletion>();
    RenderFramePacket first_packet = make_packet(first_frame, first_completion);
    RenderSceneUpdateBatch scene_batch;
    scene_batch.scene_id = allocate_render_id<RenderSceneId>();
    ViewportFrame viewport_frame;
    viewport_frame.viewport_id = ViewportId(1);
    SceneViewFamilyFrame scene_frame;
    scene_frame.view_family.scene_id = scene_batch.scene_id;
    scene_frame.view_family.views.push_back(SceneView{});
    scene_frame.output.output_id = SceneOutputId(1);
    scene_frame.output.extent = {1280, 720};
    viewport_frame.scene_frames.push_back(std::move(scene_frame));
    first_packet.viewport_frames.push_back(std::move(viewport_frame));
    first_packet.scene_updates.push_back(std::move(scene_batch));
    MaterialRenderResourceUpdate resource_update;
    resource_update.resource_id = MaterialRenderResourceId(1);
    auto material_version = std::make_shared<MaterialRenderResourceVersion>();
    material_version->resource_id = resource_update.resource_id;
    material_version->revision = RenderResourceRevision(1);
    material_version->material.shader_name = "Builtin/TransportTest";
    resource_update.version = std::move(material_version);
    first_packet.resource_updates.push_back(std::move(resource_update));
    check(queue.enqueue(std::move(first_packet)) == RenderFrameEnqueueResult::Accepted,
        "The queue must accept its first valid frame");

    auto second_enqueue = std::async(
        std::launch::async,
        [&queue, second_frame, second_completion]()
        {
            return queue.enqueue(make_packet(second_frame, second_completion));
        });
    check(second_enqueue.wait_for(20ms) == std::future_status::timeout,
        "A second queued frame must wait for bounded queue capacity");

    RenderFramePacket dequeued_packet;
    check(queue.wait_dequeue(dequeued_packet) == RenderFrameDequeueResult::Packet &&
        dequeued_packet.frame_id == first_frame &&
        dequeued_packet.resource_updates.size() == 1 &&
        dequeued_packet.scene_updates.size() == 1 &&
        dequeued_packet.viewport_frames.size() == 1 &&
        dequeued_packet.viewport_frames[0].scene_frames.size() == 1 &&
        dequeued_packet.viewport_frames[0].scene_frames[0].view_family.views.size() == 1,
        "The consumer must receive the complete owned resource, scene, and view packet in FIFO order");
    check(second_enqueue.wait_for(1s) == std::future_status::ready &&
        second_enqueue.get() == RenderFrameEnqueueResult::Accepted,
        "Dequeuing the processing frame must release capacity for one queued frame");
    check(dequeued_packet.completion->complete_success(),
        "The consumer must be able to complete a dequeued frame");
    check(first_completion->wait().state == RenderFrameCompletionState::Succeeded,
        "The producer and consumer must share the same completion");

    queue.stop_accepting();
    check(!queue.is_accepting(),
        "stop_accepting() must reject new frames");
    RenderFramePacket second_dequeued_packet;
    check(queue.wait_dequeue(second_dequeued_packet) == RenderFrameDequeueResult::Packet &&
        second_dequeued_packet.frame_id == second_frame,
        "Graceful stop must drain an already queued frame");
    check(queue.wait_dequeue(dequeued_packet) == RenderFrameDequeueResult::Stopped,
        "A stopped and drained queue must end consumer waiting");
    check(second_dequeued_packet.completion->complete_success(),
        "A frame drained during graceful stop must still complete normally");

    auto stopped_completion = std::make_shared<RenderFrameCompletion>();
    check(queue.enqueue(make_packet(
        allocate_render_id<RenderFrameId>(), stopped_completion)) ==
        RenderFrameEnqueueResult::Stopped,
        "A stopped queue must reject later submissions");
    check(stopped_completion->wait().state == RenderFrameCompletionState::Cancelled,
        "A rejected submission must always signal its completion");

    RenderFrameQueue abort_queue;
    auto aborted_completion = std::make_shared<RenderFrameCompletion>();
    check(abort_queue.enqueue(make_packet(
        allocate_render_id<RenderFrameId>(), aborted_completion)) ==
        RenderFrameEnqueueResult::Accepted,
        "The abort test frame must enter the queue");
    abort_queue.abort_pending("render dispatcher terminated");
    const RenderFrameCompletionResult aborted_result = aborted_completion->wait();
    check(aborted_result.state == RenderFrameCompletionState::Cancelled &&
        aborted_result.message == "render dispatcher terminated" &&
        abort_queue.empty(),
        "abort_pending() must cancel every packet still owned by the queue");

    RenderFrameQueue invalid_queue;
    auto invalid_completion = std::make_shared<RenderFrameCompletion>();
    RenderFramePacket invalid_packet = make_packet({}, invalid_completion);
    invalid_packet.timing.delta_seconds =
        std::numeric_limits<double>::quiet_NaN();
    check(invalid_queue.enqueue(std::move(invalid_packet)) ==
        RenderFrameEnqueueResult::InvalidPacket,
        "The transport boundary must reject an invalid frame identity or timing");
    check(invalid_completion->wait().state == RenderFrameCompletionState::Cancelled,
        "An invalid packet with a completion must not leave a waiter blocked");

    const std::thread::id main_thread = std::this_thread::get_id();
    auto threaded_state = std::make_shared<FakeProcessorState>();
    RenderFrameDispatcherConfig threaded_config;
    threaded_config.multithreaded = true;
    threaded_config.one_frame_thread_lag = true;
    RenderFrameDispatcher threaded_dispatcher(
        threaded_config,
        std::make_unique<FakeRenderFrameProcessor>(threaded_state));
    check(static_cast<bool>(threaded_dispatcher.start()),
        "A threaded RenderFrameDispatcher must synchronously initialize its processor");
    check(threaded_dispatcher.is_running(),
        "A successfully initialized RenderFrameDispatcher must enter the running state");

    const RenderFrameId lag_first_frame = allocate_render_id<RenderFrameId>();
    const RenderFrameId lag_second_frame = allocate_render_id<RenderFrameId>();
    auto lag_first_completion = std::make_shared<RenderFrameCompletion>();
    auto lag_second_completion = std::make_shared<RenderFrameCompletion>();
    const RenderFrameSubmitResult lag_first_submit = threaded_dispatcher.submit(
        make_packet(lag_first_frame, lag_first_completion));
    check(lag_first_submit.enqueue_result == RenderFrameEnqueueResult::Accepted &&
        !lag_first_submit.has_completed_frame(),
        "The first lagged frame must submit without waiting for a completion");
    const RenderFrameSubmitResult lag_second_submit = threaded_dispatcher.submit(
        make_packet(lag_second_frame, lag_second_completion));
    check(lag_second_submit.enqueue_result == RenderFrameEnqueueResult::Accepted &&
        lag_second_submit.completed_frame_id == lag_first_frame &&
        lag_second_submit.completed_frame.state == RenderFrameCompletionState::Succeeded,
        "Submitting frame N with lag enabled must return frame N-1 completion");
    check(static_cast<bool>(threaded_dispatcher.flush()),
        "flush() must drain the lagged frame and execute the processor flush");
    check(lag_second_completion->is_complete(),
        "flush() must leave no accepted frame completion pending");
    check(static_cast<bool>(threaded_dispatcher.shutdown()),
        "A threaded RenderFrameDispatcher must shut down cleanly");
    check(static_cast<bool>(threaded_dispatcher.shutdown()),
        "RenderFrameDispatcher shutdown must be idempotent");
    {
        std::lock_guard<std::mutex> lock(threaded_state->mutex);
        check(threaded_state->initialize_count == 1 &&
            threaded_state->processed_frames.size() == 2 &&
            threaded_state->flush_count == 1 &&
            threaded_state->shutdown_count == 1,
            "The threaded processor lifecycle must execute each control phase exactly once");
        check(threaded_state->initialize_thread != main_thread &&
            threaded_state->process_thread == threaded_state->initialize_thread &&
            threaded_state->flush_thread == threaded_state->initialize_thread &&
            threaded_state->shutdown_thread == threaded_state->initialize_thread,
            "Every threaded RenderFrameDispatcher phase must execute on the owning Render thread");
    }

    auto threaded_sync_state = std::make_shared<FakeProcessorState>();
    const RenderFrameId threaded_failing_frame = allocate_render_id<RenderFrameId>();
    threaded_sync_state->failing_frame = threaded_failing_frame;
    RenderFrameDispatcherConfig threaded_sync_config;
    threaded_sync_config.multithreaded = true;
    threaded_sync_config.one_frame_thread_lag = false;
    RenderFrameDispatcher threaded_sync_dispatcher(
        threaded_sync_config,
        std::make_unique<FakeRenderFrameProcessor>(threaded_sync_state));
    check(static_cast<bool>(threaded_sync_dispatcher.start()),
        "A threaded RenderFrameDispatcher with lag disabled must start");
    auto threaded_failure_completion = std::make_shared<RenderFrameCompletion>();
    const RenderFrameSubmitResult threaded_failure_submit = threaded_sync_dispatcher.submit(
        make_packet(threaded_failing_frame, threaded_failure_completion));
    check(threaded_failure_submit.completed_frame_id == threaded_failing_frame &&
        threaded_failure_submit.completed_frame.state == RenderFrameCompletionState::Failed &&
        threaded_failure_submit.completed_frame.message == "fake frame failure" &&
        threaded_sync_dispatcher.is_running(),
        "A normal threaded processing failure must complete frame N without terminating the dispatcher");
    check(static_cast<bool>(threaded_sync_dispatcher.shutdown()),
        "The threaded no-lag RenderFrameDispatcher must shut down after a frame failure");

    auto fatal_state = std::make_shared<FakeProcessorState>();
    const RenderFrameId fatal_frame = allocate_render_id<RenderFrameId>();
    fatal_state->fatal_frame = fatal_frame;
    RenderFrameDispatcher fatal_dispatcher(
        threaded_sync_config,
        std::make_unique<FakeRenderFrameProcessor>(fatal_state));
    check(static_cast<bool>(fatal_dispatcher.start()),
        "The fatal outcome RenderFrameDispatcher must start");
    auto fatal_completion = std::make_shared<RenderFrameCompletion>();
    const RenderFrameSubmitResult fatal_submit = fatal_dispatcher.submit(
        make_packet(fatal_frame, fatal_completion));
    check(fatal_submit.completed_frame_id == fatal_frame &&
        fatal_submit.completed_frame.state == RenderFrameCompletionState::Fatal &&
        fatal_submit.completed_frame.message == "fake fatal renderer failure" &&
        !fatal_dispatcher.is_running(),
        "FatalRenderer must be explicit in the completion and stop the RenderFrameDispatcher");
    check(static_cast<bool>(fatal_dispatcher.shutdown()),
        "A terminal RenderFrameDispatcher must still complete ordered shutdown");

    auto inline_state = std::make_shared<FakeProcessorState>();
    const RenderFrameId inline_failing_frame = allocate_render_id<RenderFrameId>();
    inline_state->failing_frame = inline_failing_frame;
    RenderFrameDispatcherConfig inline_config;
    inline_config.multithreaded = false;
    inline_config.one_frame_thread_lag = true;
    RenderFrameDispatcher inline_dispatcher(
        inline_config,
        std::make_unique<FakeRenderFrameProcessor>(inline_state));
    check(!inline_dispatcher.config().one_frame_thread_lag,
        "Single-thread fallback must force one-frame lag off");
    check(static_cast<bool>(inline_dispatcher.start()),
        "A single-thread RenderFrameDispatcher must initialize synchronously");
    auto inline_completion = std::make_shared<RenderFrameCompletion>();
    const RenderFrameSubmitResult inline_submit = inline_dispatcher.submit(
        make_packet(inline_failing_frame, inline_completion));
    check(inline_submit.completed_frame_id == inline_failing_frame &&
        inline_submit.completed_frame.state == RenderFrameCompletionState::Failed &&
        inline_submit.completed_frame.message == "fake frame failure",
        "Single-thread submit must synchronously return the current frame failure");
    check(inline_dispatcher.flush() && inline_dispatcher.shutdown(),
        "Single-thread flush and shutdown must complete synchronously");
    {
        std::lock_guard<std::mutex> lock(inline_state->mutex);
        check(inline_state->initialize_thread == main_thread &&
            inline_state->process_thread == main_thread &&
            inline_state->flush_thread == main_thread &&
            inline_state->shutdown_thread == main_thread,
            "Single-thread fallback must run the same processor phases on the caller thread");
    }

    auto drain_state = std::make_shared<FakeProcessorState>();
    RenderFrameDispatcher drain_dispatcher(
        threaded_config,
        std::make_unique<FakeRenderFrameProcessor>(drain_state));
    check(static_cast<bool>(drain_dispatcher.start()),
        "The shutdown drain RenderFrameDispatcher must start");
    auto drain_completion = std::make_shared<RenderFrameCompletion>();
    check(drain_dispatcher.submit(make_packet(
        allocate_render_id<RenderFrameId>(), drain_completion)).enqueue_result ==
        RenderFrameEnqueueResult::Accepted,
        "The shutdown drain frame must be accepted");
    check(drain_dispatcher.shutdown() && drain_completion->is_complete(),
        "shutdown() must drain accepted work and leave no completion pending");

    auto stopped_submit_completion = std::make_shared<RenderFrameCompletion>();
    check(drain_dispatcher.submit(make_packet(
        allocate_render_id<RenderFrameId>(), stopped_submit_completion)).enqueue_result ==
        RenderFrameEnqueueResult::Stopped &&
        stopped_submit_completion->wait().state == RenderFrameCompletionState::Cancelled,
        "Submission after shutdown must be rejected with a terminal completion");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " render frame transport checks failed.\n";
        return 1;
    }

    std::cout << "Render frame transport checks passed.\n";
    return 0;
}
