#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "exchange/net.hpp"

using namespace exchange;

namespace {

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

double percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        p / 100.0 * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[index]);
}

struct ClientStats {
    std::vector<std::uint64_t> latencies_ns;
    std::uint64_t errors = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::size_t clients = 4;
    std::size_t requests = 10'000;
    std::uint16_t port = 9001;
    try {
        if (argc > 1) clients = static_cast<std::size_t>(std::stoull(argv[1]));
        if (argc > 2) requests = static_cast<std::size_t>(std::stoull(argv[2]));
        if (argc > 3) port = static_cast<std::uint16_t>(std::stoi(argv[3]));
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    std::vector<ClientStats> stats(clients);
    std::vector<std::thread> threads;
    std::atomic<bool> go{false};

    for (std::size_t i = 0; i < clients; ++i) {
        threads.emplace_back([&, i] {
            ClientStats& local = stats[i];
            local.latencies_ns.reserve(requests);
            const int fd = net::connect_to("127.0.0.1", port);
            if (fd < 0) {
                ++local.errors;
                return;
            }
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::size_t r = 0; r < requests; ++r) {
                const auto start = now_ns();
                if (!net::send_frame(fd, "Q")) {
                    ++local.errors;
                    break;
                }
                const auto response = net::recv_frame(fd);
                if (!response) {
                    ++local.errors;
                    break;
                }
                local.latencies_ns.push_back(now_ns() - start);
            }
            ::close(fd);
        });
    }

    const auto start = now_ns();
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    const auto elapsed_ns = now_ns() - start;

    std::vector<std::uint64_t> all;
    std::uint64_t errors = 0;
    for (auto& client : stats) {
        errors += client.errors;
        all.insert(all.end(), client.latencies_ns.begin(),
                   client.latencies_ns.end());
    }
    std::sort(all.begin(), all.end());
    const double seconds = static_cast<double>(elapsed_ns) / 1e9;
    const double qps = seconds > 0.0
        ? static_cast<double>(all.size()) / seconds
        : 0.0;

    std::cout << "clients=" << clients << " requests_per_client=" << requests
              << " completed=" << all.size() << " errors=" << errors
              << " elapsed_ms=" << elapsed_ns / 1'000'000.0
              << " qps=" << qps << '\n'
              << "latency_ns p50=" << percentile(all, 50.0)
              << " p95=" << percentile(all, 95.0)
              << " p99=" << percentile(all, 99.0)
              << " max=" << (all.empty() ? 0 : all.back()) << '\n';
    return errors == 0 ? 0 : 1;
}
