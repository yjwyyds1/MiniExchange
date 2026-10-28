#include "exchange/spsc.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_fifo_and_full_capacity() {
    exchange::SpscQueue<int, 3> queue;
    int value = -1;

    require(!queue.try_pop(value), "empty queue should reject pop");
    require(queue.try_push(10), "first push failed");
    require(queue.try_push(20), "second push failed");
    require(queue.try_push(30), "capacity should allow three elements");
    require(!queue.try_push(40), "full queue should reject push");

    require(queue.try_pop(value) && value == 10, "first FIFO value incorrect");
    require(queue.try_pop(value) && value == 20, "second FIFO value incorrect");
    require(queue.try_push(40), "push after pop should wrap into free slot");
    require(queue.try_pop(value) && value == 30, "third FIFO value incorrect");
    require(queue.try_pop(value) && value == 40, "wrapped FIFO value incorrect");
    require(!queue.try_pop(value), "drained queue should be empty");
}

void test_sticky_overflow_visible_while_full() {
    exchange::SpscQueue<int, 2> queue;
    int value = -1;

    require(!queue.overflowed(), "new queue should have no overflow");
    require(queue.try_push(1) && queue.try_push(2), "queue should hold Capacity elements");
    require(!queue.try_push(3), "full queue should reject a new element");
    require(queue.overflowed(), "consumer should see overflow while queue is still full");
    require(queue.try_pop(value) && value == 1, "first value changed after overflow");
    require(queue.try_push(4), "queue should accept a value after a pop");
    require(queue.overflowed(), "overflow should remain sticky after capacity returns");
    require(queue.try_pop(value) && value == 2, "second value changed after overflow");
    require(queue.try_pop(value) && value == 4, "later value missing after overflow");
}

void test_concurrent_fifo() {
    constexpr std::uint64_t count = 100'000;
    exchange::SpscQueue<std::uint64_t, 64> queue;
    std::atomic<bool> producer_done{false};
    std::atomic<bool> order_valid{true};
    std::atomic<std::uint64_t> received{0};

    std::thread producer([&] {
        for (std::uint64_t next = 0; next < count; ++next) {
            while (!queue.try_push(next)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t expected = 0;
        std::uint64_t value = 0;
        while (expected < count) {
            if (queue.try_pop(value)) {
                if (value != expected) {
                    order_valid.store(false, std::memory_order_relaxed);
                }
                ++expected;
            } else {
                std::this_thread::yield();
            }
        }
        received.store(expected, std::memory_order_release);
    });

    producer.join();
    consumer.join();
    require(producer_done.load(std::memory_order_acquire), "producer stopped early");
    require(received.load(std::memory_order_acquire) == count, "consumer lost values");
    require(order_valid.load(std::memory_order_relaxed), "consumer observed lost or out-of-order values");
}

}  // namespace

int main() {
    test_fifo_and_full_capacity();
    test_sticky_overflow_visible_while_full();
    test_concurrent_fifo();
    std::cout << "SPSC tests passed\n";
}
