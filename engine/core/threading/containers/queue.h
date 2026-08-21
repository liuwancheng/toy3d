#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <functional>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace toy3d
{
    enum class QueueMode
    {
        Spsc,
        Mpsc
    };

    template<typename T, QueueMode Mode = QueueMode::Spsc>
    class Queue final
    {
    public:
        Queue()
        {
            Node* dummy = new Node();
            head_.store(dummy, std::memory_order_relaxed);
            tail_ = dummy;
        }

        ~Queue()
        {
            Node* node = tail_;
            while (node != nullptr)
            {
                Node* next = node->next.load(std::memory_order_relaxed);
                delete node;
                node = next;
            }
        }

        Queue(const Queue&) = delete;
        Queue& operator=(const Queue&) = delete;

        bool enqueue(T value)
        {
            Node* node = new (std::nothrow) Node(std::move(value));
            if (node == nullptr)
            {
                return false;
            }

            Node* previous = nullptr;
            if constexpr (Mode == QueueMode::Mpsc)
            {
                // MPSC producers need one atomic publication order; the consumer remains
                // single-owner and reclaims nodes only after observing the release below.
                previous = head_.exchange(node, std::memory_order_acq_rel);
            }
            else
            {
                previous = head_.load(std::memory_order_relaxed);
                head_.store(node, std::memory_order_relaxed);
            }
            previous->next.store(node, std::memory_order_release);
            return true;
        }

        bool dequeue(T& value)
        {
            assert_consumer_thread();
            Node* next = tail_->next.load(std::memory_order_acquire);
            if (next == nullptr)
            {
                return false;
            }

            value = std::move(*next->value);
            delete tail_;
            tail_ = next;
            return true;
        }

        bool is_empty() const
        {
            assert_consumer_thread();
            return tail_->next.load(std::memory_order_acquire) == nullptr;
        }

    private:
        struct Node
        {
            Node() = default;

            explicit Node(T node_value)
                : value(std::move(node_value))
            {
            }

            std::atomic<Node*> next{nullptr};
            // std::optional lets the dummy node exist without requiring T to be default constructible.
            std::optional<T> value;
        };

        void assert_consumer_thread() const
        {
#ifndef NDEBUG
            const std::size_t current_thread =
                std::hash<std::thread::id>{}(std::this_thread::get_id());
            std::size_t expected = 0;
            if (!consumer_thread_.compare_exchange_strong(
                    expected, current_thread, std::memory_order_relaxed))
            {
                assert(expected == current_thread &&
                    "Queue dequeue and is_empty require one owner consumer");
            }
#endif
        }

        std::atomic<Node*> head_{nullptr};
        Node* tail_ = nullptr;
#ifndef NDEBUG
        mutable std::atomic<std::size_t> consumer_thread_{0};
#endif
    };
}
