#pragma once

#include "renderscene/render_frame_packet.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>

namespace toy3d
{
    enum class RenderFrameEnqueueResult
    {
        Accepted,
        InvalidPacket,
        Stopped
    };

    enum class RenderFrameDequeueResult
    {
        Packet,
        Interrupted,
        Stopped
    };

    bool is_valid_render_frame_packet(const RenderFramePacket& packet);

    class RenderFrameQueue final
    {
    public:
        RenderFrameQueue() = default;
        ~RenderFrameQueue();

        RenderFrameQueue(const RenderFrameQueue&) = delete;
        RenderFrameQueue& operator=(const RenderFrameQueue&) = delete;

        RenderFrameEnqueueResult enqueue(RenderFramePacket packet);
        RenderFrameDequeueResult wait_dequeue(RenderFramePacket& packet);

        // RenderFrameDispatcher control operations wake the consumer so flush can run on
        // the owning thread even when no frame packet is currently queued.
        void interrupt_wait();

        void stop_accepting();
        void abort_pending(std::string message);

        bool is_accepting() const;
        bool empty() const;

    private:
        // A dequeued packet is the single processing frame. Keeping one more
        // packet here gives the designed one-processing plus one-queued bound.
        static constexpr std::size_t max_queued_packet_count = 1;

        mutable std::mutex mutex_;
        std::condition_variable queue_changed_;
        std::deque<RenderFramePacket> packets_;
        bool accepting_ = true;
        bool interrupted_ = false;
    };
}
