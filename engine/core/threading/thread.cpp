#include "threading/thread.h"

#include "threading/runnable.h"
#include "threading/runnable_thread.h"

#include <stdexcept>
#include <utility>

namespace toy3d
{
    namespace
    {
        class FunctionRunnable final : public Runnable
        {
          public:
            explicit FunctionRunnable(ThreadFunction function) : function_(std::move(function)) {}

            std::uint32_t run() override
            {
                function_();
                return 0;
            }

          private:
            ThreadFunction function_;
        };
    } // namespace

    Thread::Thread(ThreadManager& thread_manager, std::string name, ThreadFunction function)
    {
        if (!function)
        {
            throw std::invalid_argument("Thread requires a function");
        }
        RunnableThreadCreateResult result = RunnableThread::create(
            thread_manager, std::make_unique<FunctionRunnable>(std::move(function)), {std::move(name)});
        if (!result.succeeded())
        {
            throw std::runtime_error(result.status().message);
        }
        thread_ = result.take_thread();
    }

    Thread::~Thread() = default;

    bool Thread::is_joinable() const
    {
        return thread_ && thread_->joinable();
    }

    void Thread::join()
    {
        if (thread_)
        {
            const ThreadStatus status = thread_->wait_for_completion();
            if (!status.succeeded())
            {
                throw std::runtime_error(status.message);
            }
        }
    }

    std::thread::id Thread::get_thread_id() const
    {
        return thread_ ? thread_->get_thread_id() : std::thread::id{};
    }
} // namespace toy3d
