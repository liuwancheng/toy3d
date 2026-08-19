#include "renderscene/render_frame_queue.h"

#include <cmath>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool is_valid(const RenderFramePacket& packet)
        {
            return packet.frame_id &&
                packet.completion != nullptr &&
                std::isfinite(packet.timing.delta_seconds) &&
                packet.timing.delta_seconds >= 0.0 &&
                std::isfinite(packet.timing.total_seconds) &&
                packet.timing.total_seconds >= 0.0;
        }

        void cancel_packet(RenderFramePacket& packet, const std::string& message)
        {
            if (packet.completion != nullptr)
            {
                packet.completion->cancel(message);
            }
        }
    }

    RenderFrameQueue::~RenderFrameQueue()
    {
        abort_pending("The RenderFrameQueue was destroyed before the frame was processed.");
    }

    RenderFrameEnqueueResult RenderFrameQueue::enqueue(RenderFramePacket packet)
    {
        if (!is_valid(packet))
        {
            cancel_packet(packet, "The RenderFramePacket is invalid.");
            return RenderFrameEnqueueResult::InvalidPacket;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        queue_changed_.wait(lock, [this]()
        {
            return packets_.size() < max_queued_packet_count || !accepting_;
        });
        if (!accepting_)
        {
            lock.unlock();
            cancel_packet(packet, "The RenderFrameQueue is no longer accepting frames.");
            return RenderFrameEnqueueResult::Stopped;
        }

        packets_.push_back(std::move(packet));
        lock.unlock();
        queue_changed_.notify_all();
        return RenderFrameEnqueueResult::Accepted;
    }

    bool RenderFrameQueue::wait_dequeue(RenderFramePacket& packet)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_changed_.wait(lock, [this]()
        {
            return !packets_.empty() || !accepting_;
        });
        if (packets_.empty())
        {
            return false;
        }

        packet = std::move(packets_.front());
        packets_.pop_front();
        lock.unlock();
        queue_changed_.notify_all();
        return true;
    }

    void RenderFrameQueue::stop_accepting()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            accepting_ = false;
        }
        queue_changed_.notify_all();
    }

    void RenderFrameQueue::abort_pending(std::string message)
    {
        std::deque<RenderFramePacket> pending_packets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            accepting_ = false;
            pending_packets.swap(packets_);
        }
        queue_changed_.notify_all();

        // Completion callbacks run without the queue mutex so waiting code can
        // safely continue into other transport operations.
        for (RenderFramePacket& packet : pending_packets)
        {
            cancel_packet(packet, message);
        }
    }

    bool RenderFrameQueue::is_accepting() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepting_;
    }

    bool RenderFrameQueue::empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return packets_.empty();
    }
}
