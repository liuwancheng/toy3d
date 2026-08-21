#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace toy3d
{
    class RunnableThread;
    class ThreadManager;

    using ThreadFunction = std::function<void()>;

    class Thread final
    {
    public:
        Thread(ThreadManager& thread_manager, std::string name, ThreadFunction function);
        ~Thread();

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        bool is_joinable() const;
        void join();
        std::thread::id get_thread_id() const;

    private:
        std::unique_ptr<RunnableThread> thread_;
    };
}
