#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace toy3d
{
    namespace detail
    {
        template <typename T, typename Sequence = std::size_t> class BoundedMpmcQueue final
        {
            static_assert(std::is_pointer<T>::value,
                          "BoundedMpmcQueue is an internal Task Graph queue for task pointers");
            static_assert(std::is_unsigned<Sequence>::value, "BoundedMpmcQueue sequence counters must be unsigned");

          public:
            explicit BoundedMpmcQueue(std::size_t capacity)
                : capacity_(capacity), mask_(capacity - 1), cells_(create_cells(capacity))
            {
                for (std::size_t index = 0; index < capacity_; ++index)
                {
                    cells_[index].sequence.store(static_cast<Sequence>(index), std::memory_order_relaxed);
                }
            }

            BoundedMpmcQueue(const BoundedMpmcQueue&) = delete;
            BoundedMpmcQueue& operator=(const BoundedMpmcQueue&) = delete;

            bool try_enqueue(T value)
            {
                Sequence position = enqueue_position_.load(std::memory_order_relaxed);
                for (;;)
                {
                    Cell& cell = cells_[static_cast<std::size_t>(position) & mask_];
                    const Sequence sequence = cell.sequence.load(std::memory_order_acquire);
                    const Sequence distance = static_cast<Sequence>(sequence - position);
                    if (distance == 0)
                    {
                        const Sequence next = static_cast<Sequence>(position + 1);
                        if (enqueue_position_.compare_exchange_weak(position, next, std::memory_order_relaxed))
                        {
                            cell.value = value;
                            cell.sequence.store(next, std::memory_order_release);
                            return true;
                        }
                    }
                    else if (is_behind(distance))
                    {
                        return false;
                    }
                    else
                    {
                        position = enqueue_position_.load(std::memory_order_relaxed);
                    }
                }
            }

            bool try_dequeue(T& value)
            {
                Sequence position = dequeue_position_.load(std::memory_order_relaxed);
                for (;;)
                {
                    Cell& cell = cells_[static_cast<std::size_t>(position) & mask_];
                    const Sequence expected = static_cast<Sequence>(position + 1);
                    const Sequence sequence = cell.sequence.load(std::memory_order_acquire);
                    const Sequence distance = static_cast<Sequence>(sequence - expected);
                    if (distance == 0)
                    {
                        const Sequence next = expected;
                        if (dequeue_position_.compare_exchange_weak(position, next, std::memory_order_relaxed))
                        {
                            value = cell.value;
                            cell.value = nullptr;
                            cell.sequence.store(static_cast<Sequence>(position + capacity_), std::memory_order_release);
                            return true;
                        }
                    }
                    else if (is_behind(distance))
                    {
                        return false;
                    }
                    else
                    {
                        position = dequeue_position_.load(std::memory_order_relaxed);
                    }
                }
            }

            std::size_t capacity() const { return capacity_; }

          private:
            struct Cell
            {
                std::atomic<Sequence> sequence{0};
                T value = nullptr;
            };

            static std::unique_ptr<Cell[]> create_cells(std::size_t capacity)
            {
                const std::size_t maximum_capacity = static_cast<std::size_t>(std::numeric_limits<Sequence>::max()) / 2;
                if (capacity < 2 || (capacity & (capacity - 1)) != 0 || capacity > maximum_capacity)
                {
                    throw std::invalid_argument("BoundedMpmcQueue capacity must be a supported power of two");
                }
                return std::make_unique<Cell[]>(capacity);
            }

            static bool is_behind(Sequence distance)
            {
                constexpr Sequence half_range = static_cast<Sequence>(std::numeric_limits<Sequence>::max() / 2 + 1);
                return distance >= half_range;
            }

            static constexpr std::size_t cache_line_size = 64;

            const std::size_t capacity_;
            const std::size_t mask_;
            std::unique_ptr<Cell[]> cells_;
            alignas(cache_line_size) std::atomic<Sequence> enqueue_position_{0};
            alignas(cache_line_size) std::atomic<Sequence> dequeue_position_{0};
        };
    } // namespace detail
} // namespace toy3d
