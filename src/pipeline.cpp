#include "exchange/pipeline.hpp"

#include <chrono>
#include <thread>

#include "exchange/spsc.hpp"

namespace exchange {

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

PipelineStats run_pipeline(std::vector<Event> events, MatchingEngine& engine) {
    PipelineStats stats;
    stats.events = events.size();
    stats.latencies_ns.reserve(events.size());
    stats.queue_wait_ns.reserve(events.size());
    stats.service_ns.reserve(events.size());

    SpscQueue<Event, kPipelineCapacity> queue;
    const std::size_t total = events.size();

    // 生产者：事件生成后可立即入队；队列满时自旋让出 CPU（订单不能丢）
    std::thread producer([&] {
        for (auto& event : events) {
            event.enqueue_ns = now_ns();
            while (!queue.try_push(event)) {
                std::this_thread::yield();
            }
        }
    });

    // 消费者：撮合核心永远单线程，保证确定性与无锁
    std::thread consumer([&] {
        Event event;
        std::size_t received = 0;
        while (received < total) {
            if (queue.try_pop(event)) {
                const auto dequeued_ns = now_ns();
                const auto queue_wait_ns = dequeued_ns - event.enqueue_ns;
                if (event.kind == Event::Kind::CancelOrder) {
                    engine.cancel(event.cancel_id);
                } else {
                    stats.trades += engine.submit(event.order).trades.size();
                }
                const auto processed_ns = now_ns();
                stats.queue_wait_ns.push_back(queue_wait_ns);
                stats.service_ns.push_back(processed_ns - dequeued_ns);
                stats.latencies_ns.push_back(processed_ns - event.enqueue_ns);
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    stats.final_snapshot = engine.snapshot();
    return stats;
}

}  // namespace exchange
