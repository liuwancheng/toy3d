#include "threading/event.h"

namespace toy3d
{
    Event::Event(EventMode mode) : mode_(mode) {}

    void Event::trigger()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signaled_ = true;
        }

        if (mode_ == EventMode::ManualReset)
        {
            condition_.notify_all();
        }
        else
        {
            condition_.notify_one();
        }
    }

    void Event::reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        signaled_ = false;
    }

    void Event::wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() { return signaled_; });
        consume_signal_locked();
    }

    bool Event::wait_for(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, timeout, [this]() { return signaled_; }))
        {
            return false;
        }
        return consume_signal_locked();
    }

    bool Event::consume_signal_locked()
    {
        if (mode_ == EventMode::AutoReset)
        {
            signaled_ = false;
        }
        return true;
    }
} // namespace toy3d
