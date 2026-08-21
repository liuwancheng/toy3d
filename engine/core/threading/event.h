#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace toy3d
{
    enum class EventMode
    {
        AutoReset,
        ManualReset
    };

    class Event final
    {
    public:
        explicit Event(EventMode mode = EventMode::AutoReset);

        void trigger();
        void reset();
        void wait();
        bool wait_for(std::chrono::milliseconds timeout);

    private:
        bool consume_signal_locked();

        EventMode mode_ = EventMode::AutoReset;
        bool signaled_ = false;
        std::mutex mutex_;
        std::condition_variable condition_;
    };

    using EventRef = std::shared_ptr<Event>;
}
