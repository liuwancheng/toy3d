#include "renderscene/render_frame_queue.h"

#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

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
    first_packet.scene_updates.push_back(std::move(scene_batch));
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
    check(queue.wait_dequeue(dequeued_packet) &&
        dequeued_packet.frame_id == first_frame &&
        dequeued_packet.scene_updates.size() == 1,
        "The consumer must receive the complete owned packet in FIFO order");
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
    check(queue.wait_dequeue(second_dequeued_packet) &&
        second_dequeued_packet.frame_id == second_frame,
        "Graceful stop must drain an already queued frame");
    check(!queue.wait_dequeue(dequeued_packet),
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
    abort_queue.abort_pending("render role terminated");
    const RenderFrameCompletionResult aborted_result = aborted_completion->wait();
    check(aborted_result.state == RenderFrameCompletionState::Cancelled &&
        aborted_result.message == "render role terminated" &&
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

    if (failure_count != 0)
    {
        std::cerr << failure_count << " render frame transport checks failed.\n";
        return 1;
    }

    std::cout << "Render frame transport checks passed.\n";
    return 0;
}
