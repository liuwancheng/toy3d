#include "threading/event.h"
#include "threading/runnable.h"
#include "threading/runnable_thread.h"
#include "threading/thread.h"
#include "threading/thread_manager.h"

#include <atomic>
#include <chrono>
#include <cstdint>
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

    class LifecycleRunnable final : public toy3d::Runnable
    {
    public:
        LifecycleRunnable(std::vector<std::string>& calls, toy3d::Event& stop_event)
            : calls_(calls), stop_event_(stop_event)
        {
        }

        toy3d::ThreadStatus init() override
        {
            thread_id_ = std::this_thread::get_id();
            calls_.push_back("init");
            return toy3d::ThreadStatus::success();
        }

        std::uint32_t run() override
        {
            calls_.push_back("run");
            stop_event_.wait();
            return 42;
        }

        void stop() override
        {
            ++stop_count_;
            stop_event_.trigger();
        }

        void exit() override
        {
            calls_.push_back("exit");
        }

        std::thread::id thread_id_;
        std::atomic<int> stop_count_{0};

    private:
        std::vector<std::string>& calls_;
        toy3d::Event& stop_event_;
    };

    class FailingInitRunnable final : public toy3d::Runnable
    {
    public:
        explicit FailingInitRunnable(std::atomic<bool>& ran)
            : ran_(ran)
        {
        }

        toy3d::ThreadStatus init() override
        {
            return toy3d::ThreadStatus::failure(
                toy3d::ThreadErrorCode::InvalidState, "fixture init failed");
        }

        std::uint32_t run() override
        {
            ran_.store(true);
            return 0;
        }

    private:
        std::atomic<bool>& ran_;
    };

    class ThrowingRunnable final : public toy3d::Runnable
    {
    public:
        std::uint32_t run() override
        {
            throw std::runtime_error("fixture failure");
        }

        void exit() override
        {
            exited_.store(true);
        }

        std::atomic<bool> exited_{false};
    };

    class SelfJoinRunnable final : public toy3d::Runnable
    {
    public:
        explicit SelfJoinRunnable(toy3d::Event& start)
            : start_(start)
        {
        }

        std::uint32_t run() override
        {
            start_.wait();
            result_.store(thread_->wait_for_completion().code);
            return 0;
        }

        toy3d::Event& start_;
        toy3d::RunnableThread* thread_ = nullptr;
        std::atomic<toy3d::ThreadErrorCode> result_{toy3d::ThreadErrorCode::None};
    };

    void test_runnable_lifecycle_and_manager()
    {
        toy3d::ThreadManager manager;
        toy3d::Event stop_event;
        std::vector<std::string> calls;
        auto runnable = std::make_unique<LifecycleRunnable>(calls, stop_event);
        LifecycleRunnable* observed = runnable.get();

        toy3d::RunnableThreadCreateResult created = toy3d::RunnableThread::create(
            manager, std::move(runnable), {"Lifecycle"});
        check(created.succeeded(), "create must complete a successful init handshake");
        if (!created.succeeded())
        {
            return;
        }

        std::unique_ptr<toy3d::RunnableThread> thread = created.take_thread();
        check(observed->thread_id_ == thread->get_thread_id(),
            "init must run on the target thread");
        const toy3d::ThreadInfo info = manager.get_thread(thread->get_thread_id());
        check(info.id == thread->get_thread_id() && info.name == "Lifecycle",
            "ThreadManager must expose registered thread metadata");

        int enumerated = 0;
        manager.for_each_thread([&enumerated](const toy3d::ThreadInfo&) { ++enumerated; });
        check(enumerated == 1, "ThreadManager enumeration must use a stable snapshot");

        thread->request_stop();
        thread->request_stop();
        check(thread->wait_for_completion().succeeded(), "wait must join the target thread");
        check(thread->wait_for_completion().succeeded(), "repeated wait must be idempotent");
        check(!thread->joinable(), "joined thread must not remain joinable");
        check(observed->stop_count_.load() == 1, "request_stop must be idempotent");
        check(calls == std::vector<std::string>({"init", "run", "exit"}),
            "lifecycle callbacks must execute in init/run/exit order");
        check(thread->get_result().return_code == 42,
            "run return code must be retained after completion");
        check(manager.get_thread(thread->get_thread_id()).id == std::thread::id{},
            "joined thread must be removed from ThreadManager");
    }

    void test_failure_boundaries()
    {
        toy3d::ThreadManager manager;
        std::atomic<bool> ran{false};
        toy3d::RunnableThreadCreateResult failed = toy3d::RunnableThread::create(
            manager, std::make_unique<FailingInitRunnable>(ran), {"InitFailure"});
        check(!failed.succeeded() && failed.status().code == toy3d::ThreadErrorCode::InitFailed,
            "init failure must be returned as structured status");
        check(!ran.load(), "run must not execute after init failure");

        auto throwing = std::make_unique<ThrowingRunnable>();
        ThrowingRunnable* observed = throwing.get();
        toy3d::RunnableThreadCreateResult created = toy3d::RunnableThread::create(
            manager, std::move(throwing), {"RunFailure"});
        check(created.succeeded(), "a successful init must allow create to return");
        if (created.succeeded())
        {
            std::unique_ptr<toy3d::RunnableThread> thread = created.take_thread();
            thread->wait_for_completion();
            check(thread->get_result().status.code == toy3d::ThreadErrorCode::UnhandledException,
                "run exception must be captured instead of crossing the thread boundary");
            check(observed->exited_.load(), "exit must run after a run exception");
        }

        check(!toy3d::RunnableThread::create(manager, nullptr, {"Null"}).succeeded(),
            "null runnable must be rejected");
        check(!toy3d::RunnableThread::create(
                manager, std::make_unique<FailingInitRunnable>(ran), {}).succeeded(),
            "empty thread name must be rejected");
    }

    void test_self_join_is_rejected()
    {
        toy3d::ThreadManager manager;
        toy3d::Event start;
        auto runnable = std::make_unique<SelfJoinRunnable>(start);
        SelfJoinRunnable* observed = runnable.get();
        toy3d::RunnableThreadCreateResult created = toy3d::RunnableThread::create(
            manager, std::move(runnable), {"SelfJoin"});
        check(created.succeeded(), "self-join fixture must start");
        if (!created.succeeded())
        {
            return;
        }
        std::unique_ptr<toy3d::RunnableThread> thread = created.take_thread();
        observed->thread_ = thread.get();
        start.trigger();
        thread->wait_for_completion();
        check(observed->result_.load() == toy3d::ThreadErrorCode::InvalidCaller,
            "self-join must return InvalidCaller");
    }

    void test_events()
    {
        toy3d::Event auto_event;
        auto_event.trigger();
        check(auto_event.wait_for(20ms), "AutoReset signal must survive signal-before-wait");
        check(!auto_event.wait_for(20ms), "AutoReset wait must consume one signal");

        toy3d::Event auto_waiters_ready(toy3d::EventMode::ManualReset);
        toy3d::Event one_auto_waiter_released;
        std::atomic<int> auto_waiting{0};
        std::atomic<int> auto_released{0};
        std::vector<std::thread> auto_waiters;
        for (int index = 0; index < 2; ++index)
        {
            auto_waiters.emplace_back([&]()
            {
                if (auto_waiting.fetch_add(1) + 1 == 2)
                {
                    auto_waiters_ready.trigger();
                }
                auto_event.wait();
                if (auto_released.fetch_add(1) + 1 == 1)
                {
                    one_auto_waiter_released.trigger();
                }
            });
        }
        auto_waiters_ready.wait();
        auto_event.trigger();
        check(one_auto_waiter_released.wait_for(100ms) && auto_released.load() == 1,
            "one AutoReset trigger must release exactly one waiter");
        auto_event.trigger();
        for (std::thread& waiter : auto_waiters)
        {
            waiter.join();
        }

        constexpr int waiter_count = 3;
        toy3d::Event manual_event(toy3d::EventMode::ManualReset);
        toy3d::Event ready(toy3d::EventMode::ManualReset);
        std::atomic<int> waiting{0};
        std::atomic<int> released{0};
        std::vector<std::thread> waiters;
        for (int index = 0; index < waiter_count; ++index)
        {
            waiters.emplace_back([&]()
            {
                if (waiting.fetch_add(1) + 1 == waiter_count)
                {
                    ready.trigger();
                }
                manual_event.wait();
                released.fetch_add(1);
            });
        }
        ready.wait();
        manual_event.trigger();
        for (std::thread& waiter : waiters)
        {
            waiter.join();
        }
        check(released.load() == waiter_count, "ManualReset trigger must release every waiter");
        check(manual_event.wait_for(20ms), "ManualReset signal must remain set");
        manual_event.reset();
        check(!manual_event.wait_for(20ms), "ManualReset reset must clear the signal");
    }

    void test_function_thread()
    {
        toy3d::ThreadManager manager;
        std::atomic<bool> called{false};
        toy3d::Thread thread(manager, "Function", [&called]() { called.store(true); });
        const std::thread::id id = thread.get_thread_id();
        check(id != std::thread::id{}, "Thread wrapper must expose the target id");
        thread.join();
        check(called.load() && !thread.is_joinable(),
            "Thread wrapper must execute its function and join explicitly");
    }

    void test_unjoined_thread_is_diagnosed()
    {
        std::atomic<toy3d::ThreadErrorCode> diagnosed{toy3d::ThreadErrorCode::None};
        toy3d::ThreadManager manager([&diagnosed](const toy3d::ThreadStatus& status)
        {
            diagnosed.store(status.code);
        });
        {
            toy3d::Thread thread(manager, "Unjoined", []() {});
        }
        check(diagnosed.load() == toy3d::ThreadErrorCode::NotJoined,
            "destroying a joinable thread must report NotJoined");
    }
}

int main()
{
    test_runnable_lifecycle_and_manager();
    test_failure_boundaries();
    test_self_join_is_rejected();
    test_events();
    test_function_thread();
    test_unjoined_thread_is_diagnosed();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " threading test(s) failed\n";
        return 1;
    }
    std::cout << "All threading tests passed\n";
    return 0;
}
