#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "exchange/order_generator.hpp"
#include "exchange/pipeline.hpp"
#include "exchange/replay.hpp"

using namespace exchange;

namespace {

double percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        p / 100.0 * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[index]);
}

void print_percentiles(const char* label, std::vector<std::uint64_t>& samples) {
    std::sort(samples.begin(), samples.end());
    std::cout << label << " p50=" << percentile(samples, 50.0)
              << " p95=" << percentile(samples, 95.0)
              << " p99=" << percentile(samples, 99.0)
              << " max=" << (samples.empty() ? 0 : samples.back()) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t count = 1'000'000;
    std::uint64_t seed = 42;
    try {
        if (argc > 1) count = static_cast<std::size_t>(std::stoull(argv[1]));
        if (argc > 2) seed = static_cast<std::uint64_t>(std::stoull(argv[2]));
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    OrderGenerator generator(seed);
    auto events = generate_events(generator, count);

    MatchingEngine engine;
    const auto start = now_ns();
    auto stats = run_pipeline(std::move(events), engine);
    const auto elapsed_ns = now_ns() - start;

    auto& latencies = stats.latencies_ns;
    std::sort(latencies.begin(), latencies.end());
    const double elapsed_s = static_cast<double>(elapsed_ns) / 1e9;
    const double throughput = elapsed_s > 0.0
        ? static_cast<double>(stats.events) / elapsed_s
        : 0.0;

    std::cout << "events=" << stats.events << " trades=" << stats.trades << '\n'
              << "elapsed_ms=" << elapsed_ns / 1'000'000.0 << '\n'
              << "throughput_events_per_sec=" << throughput << '\n';
    print_percentiles("end_to_end_ns", stats.latencies_ns);
    print_percentiles("queue_wait_ns", stats.queue_wait_ns);
    print_percentiles("service_ns", stats.service_ns);
    std::cout << "final: bid " << stats.final_snapshot.best_bid << " x "
              << stats.final_snapshot.bid_qty << " | ask "
              << stats.final_snapshot.best_ask << " x "
              << stats.final_snapshot.ask_qty << " (seq "
              << stats.final_snapshot.seq << ")\n";
    return 0;
}
