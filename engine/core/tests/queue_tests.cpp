#include "threading/containers/bounded_mpmc_queue.h"
#include "threading/containers/queue.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    void test_spsc_exactly_once_and_fifo()
    {
        constexpr int value_count = 20000;
        toy3d::Queue<int> queue;
        std::thread producer(
            [&queue, value_count]()
            {
                for (int value = 0; value < value_count; ++value)
                {
                    if (!queue.enqueue(value))
                    {
                        throw std::runtime_error("SPSC allocation failed");
                    }
                    if ((value & 127) == 0)
                    {
                        std::this_thread::yield();
                    }
                }
            });

        for (int expected = 0; expected < value_count;)
        {
            int value = -1;
            if (queue.dequeue(value))
            {
                check(value == expected, "SPSC dequeue must preserve exact FIFO order");
                ++expected;
            }
            else
            {
                std::this_thread::yield();
            }
        }
        producer.join();
        check(queue.is_empty(), "SPSC queue must be empty after every value is consumed");
    }

    struct ProducerValue
    {
        int producer = -1;
        int sequence = -1;
    };

    void test_mpsc_exactly_once_and_producer_fifo()
    {
        constexpr int producer_count = 4;
        constexpr int values_per_producer = 5000;
        toy3d::Queue<ProducerValue, toy3d::QueueMode::Mpsc> queue;
        std::vector<std::thread> producers;
        for (int producer = 0; producer < producer_count; ++producer)
        {
            producers.emplace_back(
                [producer, values_per_producer, &queue]()
                {
                    for (int sequence = 0; sequence < values_per_producer; ++sequence)
                    {
                        if (!queue.enqueue({producer, sequence}))
                        {
                            throw std::runtime_error("MPSC allocation failed");
                        }
                        if (((producer + sequence) & 63) == 0)
                        {
                            std::this_thread::yield();
                        }
                    }
                });
        }

        std::vector<int> next_sequence(producer_count, 0);
        int consumed = 0;
        while (consumed < producer_count * values_per_producer)
        {
            ProducerValue value;
            if (!queue.dequeue(value))
            {
                std::this_thread::yield();
                continue;
            }
            check(value.producer >= 0 && value.producer < producer_count, "MPSC must return a valid producer id");
            if (value.producer >= 0 && value.producer < producer_count)
            {
                check(value.sequence == next_sequence[value.producer],
                      "MPSC must preserve FIFO order within each producer");
                ++next_sequence[value.producer];
            }
            ++consumed;
        }
        for (std::thread& producer : producers)
        {
            producer.join();
        }
        for (int producer = 0; producer < producer_count; ++producer)
        {
            check(next_sequence[producer] == values_per_producer, "MPSC must consume each producer value exactly once");
        }
    }

    struct LifetimeValue
    {
        explicit LifetimeValue(std::shared_ptr<std::atomic<int>> live_count) : live_count(std::move(live_count))
        {
            this->live_count->fetch_add(1);
        }

        LifetimeValue(LifetimeValue&& other) noexcept : live_count(std::move(other.live_count)) {}

        LifetimeValue& operator=(LifetimeValue&& other) noexcept
        {
            if (live_count)
            {
                live_count->fetch_sub(1);
            }
            live_count = std::move(other.live_count);
            return *this;
        }

        ~LifetimeValue()
        {
            if (live_count)
            {
                live_count->fetch_sub(1);
            }
        }

        std::shared_ptr<std::atomic<int>> live_count;
    };

    void test_linked_queue_destroys_remaining_values()
    {
        auto live_count = std::make_shared<std::atomic<int>>(0);
        {
            toy3d::Queue<LifetimeValue> queue;
            check(queue.enqueue(LifetimeValue(live_count)),
                  "queue must accept a move-only non-default-constructible value");
            check(queue.enqueue(LifetimeValue(live_count)), "queue must retain a second live value");
            check(live_count->load() == 2, "queued values must remain alive before destruction");
        }
        check(live_count->load() == 0, "queue destruction must release every remaining value");
    }

    struct FakeTask
    {
        int id = 0;
    };

    void test_bounded_capacity_and_sequence_wrap()
    {
        std::vector<FakeTask> tasks(5);
        toy3d::detail::BoundedMpmcQueue<FakeTask*> capacity_queue(4);
        check(capacity_queue.capacity() == 4, "bounded queue must report its fixed capacity");
        for (int index = 0; index < 4; ++index)
        {
            check(capacity_queue.try_enqueue(&tasks[index]), "bounded queue must accept values up to capacity");
        }
        check(!capacity_queue.try_enqueue(&tasks[4]), "bounded queue must reject a value while full");
        for (int index = 0; index < 4; ++index)
        {
            FakeTask* task = nullptr;
            check(capacity_queue.try_dequeue(task) && task == &tasks[index],
                  "bounded queue must return every accepted value in order");
        }

        toy3d::detail::BoundedMpmcQueue<FakeTask*, std::uint8_t> wrap_queue(4);
        for (int iteration = 0; iteration < 1000; ++iteration)
        {
            FakeTask* expected = &tasks[iteration % tasks.size()];
            check(wrap_queue.try_enqueue(expected), "bounded queue must remain writable across sequence wrap");
            FakeTask* actual = nullptr;
            check(wrap_queue.try_dequeue(actual) && actual == expected,
                  "bounded queue must remain readable across sequence wrap");
        }

        bool invalid_capacity_rejected = false;
        try
        {
            toy3d::detail::BoundedMpmcQueue<FakeTask*> invalid_queue(3);
        }
        catch (const std::invalid_argument&)
        {
            invalid_capacity_rejected = true;
        }
        check(invalid_capacity_rejected, "bounded queue must reject non-power-of-two capacity");
    }

    void test_bounded_mpmc_stress()
    {
        constexpr int producer_count = 4;
        constexpr int consumer_count = 4;
        constexpr int tasks_per_producer = 10000;
        constexpr int task_count = producer_count * tasks_per_producer;
        std::vector<FakeTask> tasks(task_count);
        std::unique_ptr<std::atomic<int>[]> seen = std::make_unique<std::atomic<int>[]>(task_count);
        for (int index = 0; index < task_count; ++index)
        {
            tasks[index].id = index;
            seen[index].store(0);
        }

        toy3d::detail::BoundedMpmcQueue<FakeTask*> queue(64);
        std::atomic<int> producers_finished{0};
        std::atomic<int> consumed{0};
        std::vector<std::thread> consumers;
        for (int consumer = 0; consumer < consumer_count; ++consumer)
        {
            consumers.emplace_back(
                [&]()
                {
                    while (consumed.load(std::memory_order_relaxed) < task_count)
                    {
                        FakeTask* task = nullptr;
                        if (queue.try_dequeue(task))
                        {
                            seen[task->id].fetch_add(1, std::memory_order_relaxed);
                            consumed.fetch_add(1, std::memory_order_relaxed);
                        }
                        else if (producers_finished.load(std::memory_order_relaxed) == producer_count)
                        {
                            std::this_thread::yield();
                        }
                        else
                        {
                            std::this_thread::yield();
                        }
                    }
                });
        }

        std::vector<std::thread> producers;
        for (int producer = 0; producer < producer_count; ++producer)
        {
            producers.emplace_back(
                [producer, tasks_per_producer, &queue, &tasks, &producers_finished]()
                {
                    const int begin = producer * tasks_per_producer;
                    const int end = begin + tasks_per_producer;
                    for (int index = begin; index < end; ++index)
                    {
                        while (!queue.try_enqueue(&tasks[index]))
                        {
                            std::this_thread::yield();
                        }
                        if ((index & 127) == 0)
                        {
                            std::this_thread::yield();
                        }
                    }
                    producers_finished.fetch_add(1, std::memory_order_relaxed);
                });
        }

        for (std::thread& producer : producers)
        {
            producer.join();
        }
        for (std::thread& consumer : consumers)
        {
            consumer.join();
        }
        check(consumed.load() == task_count, "bounded MPMC must consume every accepted task");
        for (int index = 0; index < task_count; ++index)
        {
            check(seen[index].load() == 1, "bounded MPMC must deliver each accepted task exactly once");
        }
    }
} // namespace

int main()
{
    test_spsc_exactly_once_and_fifo();
    test_mpsc_exactly_once_and_producer_fifo();
    test_linked_queue_destroys_remaining_values();
    test_bounded_capacity_and_sequence_wrap();
    test_bounded_mpmc_stress();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " queue test(s) failed\n";
        return 1;
    }
    std::cout << "All queue tests passed\n";
    return 0;
}
