#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "exchange/market_data.hpp"
#include "exchange/net.hpp"

using namespace exchange;

namespace {

constexpr const char* kMulticastGroup = "239.255.42.99";
constexpr std::uint16_t kDataPort = 9101;
constexpr std::uint16_t kControlPort = 9102;
constexpr auto kRequestInterval = std::chrono::milliseconds(50);

}  // namespace

int main(int argc, char** argv) {
    bool unicast = false;
    int duration_ms = 3000;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--unicast") {
                unicast = true;
            } else if (arg == "--duration-ms" && i + 1 < argc) {
                duration_ms = std::stoi(argv[++i]);
            } else {
                std::cerr << "未知参数: " << arg << '\n';
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    const int data = net::udp_receiver(kDataPort,
                                       unicast ? nullptr : kMulticastGroup);
    if (data < 0) {
        std::cerr << "无法加入组播/绑定端口 " << kDataPort
                  << "（可尝试 --unicast）\n";
        return 1;
    }
    const auto control = net::udp_address("127.0.0.1", kControlPort);
    if (!control) {
        std::cerr << "控制地址无效\n";
        return 1;
    }

    LocalBook book;
    std::uint64_t received = 0;
    std::uint64_t malformed = 0;
    std::uint64_t requests = 0;
    auto last_request = std::chrono::steady_clock::time_point{};

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        bool activity = false;
        while (auto packet = net::udp_recv_from(data)) {
            activity = true;
            ++received;
            const auto message = parse_md(packet->first);
            if (!message) {
                ++malformed;
                continue;
            }

            const auto result = book.on_message(*message);
            if (result == LocalBook::Result::GapDetected) {
                std::cout << "[gap] expected=" << book.expected_seq()
                          << " got=" << message->seq
                          << " missing=" << book.missing_messages() << '\n';
                // 缺口出现时立即请求快照，避免在失步状态下丢弃大量增量
                net::udp_send_to(data, *control,
                                 make_snapshot_request(book.expected_seq()));
                last_request = std::chrono::steady_clock::now();
                ++requests;
            } else if (result == LocalBook::Result::Rebuilt) {
                std::cout << "[rebuild] seq=" << message->seq
                          << " bid=" << book.best_bid() << " x "
                          << book.bid_qty() << " ask=" << book.best_ask()
                          << " x " << book.ask_qty() << '\n';
            }
        }

        if (book.needs_snapshot()) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_request >= kRequestInterval) {
                net::udp_send_to(data, *control,
                                 make_snapshot_request(book.expected_seq()));
                last_request = now;
                ++requests;
            }
        }

        if (!activity) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::cout << "received=" << received << " malformed=" << malformed
              << " trades=" << book.trades()
              << " duplicates=" << book.duplicates()
              << " gaps=" << book.gaps()
              << " missing=" << book.missing_messages()
              << " rebuilds=" << book.rebuilds()
              << " ignored=" << book.ignored()
              << " requests=" << requests << '\n'
              << "local book: bid " << book.best_bid() << " x "
              << book.bid_qty() << " | ask " << book.best_ask() << " x "
              << book.ask_qty() << " expected_seq=" << book.expected_seq()
              << " last_trade=" << book.last_trade_price() << " x "
              << book.last_trade_qty() << '\n';

    if (received == 0) {
        std::cout << "未收到行情：若组播在 WSL 不通，请两端都加 --unicast\n";
    }
    return 0;
}
