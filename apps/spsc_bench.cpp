#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "exchange/pipeline.hpp"
#include "exchange/spsc.hpp"

using namespace exchange;

namespace {

struct Payload {
    std::uint64_t sequence = 0;
    std::uint64_t enqueue_ns = 0;
};

struct Result {
    double throughput = 0.0;
    std::vector<std::uint64_t> latencies_ns;
};

double percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        p / 100.0 * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[index]);
}

template <typename PushFunction, typename PopFunction>
Result run_benchmark(std::size_t count, PushFunction push, PopFunction pop) {
    std::atomic<bool> go{false};
    Result result;
    result.latencies_ns.reserve(count);

    std::thread producer([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t i = 0; i < count; ++i) {
            Payload payload{i, now_ns()};
            push(payload);
        }
    });

    std::thread consumer([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        Payload payload;
        for (std::size_t i = 0; i < count; ++i) {
            pop(payload);
            result.latencies_ns.push_back(now_ns() - payload.enqueue_ns);
        }
    });

    const auto start = now_ns();
    go.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    const auto elapsed_ns = now_ns() - start;
    result.throughput = elapsed_ns > 0
        ? static_cast<double>(count) / (static_cast<double>(elapsed_ns) / 1e9)
        : 0.0;
    return result;
}

template <std::size_t Capacity>
Result benchmark_spsc(std::size_t count) {
    SpscQueue<Payload, Capacity> queue;
    return run_benchmark(
        count,
        [&](const Payload& payload) {
            while (!queue.try_push(payload)) {
                std::this_thread::yield();
            }
        },
        [&](Payload& payload) {
            while (!queue.try_pop(payload)) {
                std::this_thread::yield();
            }
        });
}

template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity) : capacity_(capacity) {}

    void push(const T& value) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [&] { return queue_.size() < capacity_; });
        queue_.push(value);
        not_empty_.notify_one();
    }

    void pop(T& value) {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [&] { return !queue_.empty(); });
        value = queue_.front();
        queue_.pop();
        not_full_.notify_one();
    }

private:
    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::queue<T> queue_;
    std::size_t capacity_;
};

Result benchmark_mutex(std::size_t count, std::size_t capacity) {
    MutexQueue<Payload> queue(capacity);
    return run_benchmark(
        count,
        [&](const Payload& payload) { queue.push(payload); },
        [&](Payload& payload) { queue.pop(payload); });
}

void print_row(const char* name, std::size_t capacity, std::size_t count,
               Result result) {
    auto& latencies = result.latencies_ns;
    std::sort(latencies.begin(), latencies.end());
    std::cout << name << " cap=" << capacity << " events=" << count
              << " throughput=" << result.throughput / 1e6 << " M/s"
              << " p50=" << percentile(latencies, 50.0) / 1000.0 << "us"
              << " p95=" << percentile(latencies, 95.0) / 1000.0 << "us"
              << " p99=" << percentile(latencies, 99.0) / 1000.0 << "us"
              << " max=" << (latencies.empty() ? 0 : latencies.back()) / 1000.0
              << "us\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t count = 1'000'000;
    try {
        if (argc > 1) count = static_cast<std::size_t>(std::stoull(argv[1]));
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    // 预热：让两个实现都经历一次完整的生产者/消费者启动
    constexpr std::size_t kSmallCapacity = 64;
    constexpr std::size_t kLargeCapacity = 1u << 14;
    const std::size_t warmup = std::min<std::size_t>(count, 50'000);
    benchmark_spsc<kSmallCapacity>(warmup);
    benchmark_mutex(warmup, kSmallCapacity);

    std::cout << "queue comparison, count=" << count << '\n'
              << "latency = enqueue -> dequeue（含排队等待，cap 越小排队上限越低）\n";
    print_row("spsc     ", kSmallCapacity, count,
              benchmark_spsc<kSmallCapacity>(count));
    print_row("mutex+cv ", kSmallCapacity, count,
              benchmark_mutex(count, kSmallCapacity));
    print_row("spsc     ", kLargeCapacity, count,
              benchmark_spsc<kLargeCapacity>(count));
    print_row("mutex+cv ", kLargeCapacity, count,
              benchmark_mutex(count, kLargeCapacity));
    return 0;
}
