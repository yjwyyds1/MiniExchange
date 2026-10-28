#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace exchange {

// One producer and one consumer only. A failed push leaves a sticky overflow
// signal so the consumer can stop processing even while the queue is full.
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity > 0);
public:
    bool try_push(const T& value) {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) == Capacity) {
            overflow_.store(true, std::memory_order_release);
            return false;
        }
        slots_[tail % Capacity] = value;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& value) {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        value = std::move(slots_[head % Capacity]);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool overflowed() const { return overflow_.load(std::memory_order_acquire); }
    void clear_overflow() { overflow_.store(false, std::memory_order_release); }

private:
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::atomic<bool> overflow_{false};
};

}  // namespace exchange
