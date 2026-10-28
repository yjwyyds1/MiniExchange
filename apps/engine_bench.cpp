#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "exchange/matching_engine.hpp"
#include "exchange/order_generator.hpp"

using namespace exchange;

namespace {

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

enum class Workload { LimitHeavy, CancelHeavy, TradeHeavy };

const char* workload_name(Workload workload) {
    switch (workload) {
        case Workload::LimitHeavy:  return "limit_heavy (95% limit / 5% market)";
        case Workload::CancelHeavy: return "cancel_heavy (45% limit / 5% market / 50% cancel)";
        case Workload::TradeHeavy:  return "trade_heavy (35% limit / 60% market / 5% cancel)";
    }
    return "?";
}

// 基准专用的可复现负载生成器：按 workload 调整限价/市价/撤单比例
class WorkloadGenerator {
public:
    WorkloadGenerator(Workload workload, std::uint64_t seed)
        : workload_(workload), rng_(seed) {}

    Event next() {
        const auto roll = rng_() % 100;
        int cancel_percent = 0;
        int market_percent = 5;
        switch (workload_) {
            case Workload::LimitHeavy:
                cancel_percent = 0;
                market_percent = 5;
                break;
            case Workload::CancelHeavy:
                cancel_percent = 50;
                market_percent = 5;
                break;
            case Workload::TradeHeavy:
                cancel_percent = 5;
                market_percent = 60;
                break;
        }

        if (!recent_ids_.empty() &&
            roll < static_cast<std::uint64_t>(cancel_percent)) {
            Event event;
            event.kind = Event::Kind::CancelOrder;
            event.cancel_id = recent_ids_[rng_() % recent_ids_.size()];
            return event;
        }

        Event event;
        event.kind = Event::Kind::NewOrder;
        event.order.id = next_id_++;
        event.order.side = (rng_() % 2) ? Side::Buy : Side::Sell;

        const bool market = roll < static_cast<std::uint64_t>(
            cancel_percent + market_percent);
        event.order.type = market ? OrderType::Market : OrderType::Limit;
        const auto offset = static_cast<Price>(rng_() % 200) - 100;
        event.order.price = market
            ? 0
            : 10'000 + offset + (event.order.side == Side::Buy ? -20 : 20);
        event.order.qty = rng_() % 100 + 1;

        recent_ids_.push_back(event.order.id);
        if (recent_ids_.size() > 2048) recent_ids_.pop_front();
        return event;
    }

private:
    Workload workload_;
    std::mt19937_64 rng_;
    OrderId next_id_ = 1;
    std::deque<OrderId> recent_ids_;
};

std::size_t apply_event(MatchingEngine& engine, const Event& event) {
    if (event.kind == Event::Kind::CancelOrder) {
        engine.cancel(event.cancel_id);
        return 0;
    }
    return engine.submit(event.order).trades.size();
}

struct BenchResult {
    std::size_t events = 0;
    std::size_t trades = 0;
    std::uint64_t elapsed_ns = 0;
    std::vector<std::uint64_t> latencies_ns;
};

BenchResult run(Workload workload, std::uint64_t seed, std::size_t events,
                bool use_pool, bool reserve_maps, std::size_t reset_interval) {
    WorkloadGenerator generator(workload, seed);
    MatchingEngine engine(use_pool);
    if (reserve_maps) engine.reserve(1u << 14);

    const std::size_t warmup = std::min<std::size_t>(events, 50'000);
    for (std::size_t i = 0; i < warmup; ++i) {
        apply_event(engine, generator.next());
    }

    engine = MatchingEngine(use_pool);
    if (reserve_maps) engine.reserve(1u << 14);

    BenchResult result;
    result.events = events;
    result.latencies_ns.reserve(events);

    const auto start = now_ns();
    for (std::size_t i = 0; i < events; ++i) {
        const Event event = generator.next();
        const auto before = now_ns();
        result.trades += apply_event(engine, event);
        const auto after = now_ns();
        result.latencies_ns.push_back(after - before);

        // 周期性重置，避免订单簿无限膨胀导致性能漂移
        if ((i + 1) % reset_interval == 0) {
            engine = MatchingEngine(use_pool);
            if (reserve_maps) engine.reserve(1u << 14);
        }
    }
    result.elapsed_ns = now_ns() - start;
    return result;
}

double percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        p / 100.0 * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[index]);
}

void print_result(const char* label, BenchResult result) {
    std::sort(result.latencies_ns.begin(), result.latencies_ns.end());
    const double seconds = static_cast<double>(result.elapsed_ns) / 1e9;
    const double throughput = seconds > 0.0
        ? static_cast<double>(result.events) / seconds
        : 0.0;
    std::cout << "  " << label
              << " throughput=" << throughput << " events/s"
              << " trades=" << result.trades
              << " service_ns p50=" << percentile(result.latencies_ns, 50.0)
              << " p95=" << percentile(result.latencies_ns, 95.0)
              << " p99=" << percentile(result.latencies_ns, 99.0)
              << " max=" << (result.latencies_ns.empty()
                                 ? 0
                                 : result.latencies_ns.back())
              << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t events = 1'000'000;
    std::uint64_t seed = 42;
    try {
        if (argc > 1) events = static_cast<std::size_t>(std::stoull(argv[1]));
        if (argc > 2) seed = static_cast<std::uint64_t>(std::stoull(argv[2]));
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    constexpr std::size_t kResetInterval = 50'000;
    for (const Workload workload :
         {Workload::LimitHeavy, Workload::CancelHeavy, Workload::TradeHeavy}) {
        std::cout << workload_name(workload) << " events=" << events << '\n';
        print_result("baseline", run(workload, seed, events, false, false, kResetInterval));
        print_result("pool    ", run(workload, seed, events, true, false, kResetInterval));
        print_result("pool+res", run(workload, seed, events, true, true, kResetInterval));
    }
    return 0;
}
