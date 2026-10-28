#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "exchange/market_data.hpp"
#include "exchange/matching_engine.hpp"
#include "exchange/net.hpp"
#include "exchange/order_generator.hpp"

using namespace exchange;

namespace {

constexpr const char* kMulticastGroup = "239.255.42.99";
constexpr std::uint16_t kDataPort = 9101;
constexpr std::uint16_t kControlPort = 9102;

}  // namespace

int main(int argc, char** argv) {
    std::size_t events = 100'000;
    std::uint64_t seed = 42;
    std::uint64_t drop_every = 0;
    std::uint64_t snapshot_every = 1000;
    bool unicast = false;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--unicast") {
                unicast = true;
            } else if (arg == "--events" && i + 1 < argc) {
                events = static_cast<std::size_t>(std::stoull(argv[++i]));
            } else if (arg == "--seed" && i + 1 < argc) {
                seed = static_cast<std::uint64_t>(std::stoull(argv[++i]));
            } else if (arg == "--drop-every" && i + 1 < argc) {
                drop_every = static_cast<std::uint64_t>(std::stoull(argv[++i]));
            } else if (arg == "--snapshot-every" && i + 1 < argc) {
                snapshot_every =
                    static_cast<std::uint64_t>(std::stoull(argv[++i]));
            } else {
                std::cerr << "未知参数: " << arg << '\n';
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    auto sender = net::udp_sender(unicast ? "127.0.0.1" : kMulticastGroup,
                                  kDataPort, !unicast);
    if (!sender) {
        std::cerr << "无法创建 UDP 发送端\n";
        return 1;
    }
    const int control = net::udp_control_socket(kControlPort);
    if (control < 0) {
        std::cerr << "无法绑定控制端口 " << kControlPort << '\n';
        return 1;
    }

    MatchingEngine engine;
    OrderGenerator generator(seed);
    std::uint64_t next_seq = 1;
    std::uint64_t published = 0;
    std::uint64_t dropped = 0;

    const auto send_snapshot_to = [&](int fd, const sockaddr_in& target) {
        const auto snapshot = engine.snapshot();
        MdMessage message;
        message.type = MdMessage::Type::Snapshot;
        message.seq = next_seq++;
        message.best_bid = snapshot.best_bid;
        message.bid_qty = snapshot.bid_qty;
        message.best_ask = snapshot.best_ask;
        message.ask_qty = snapshot.ask_qty;
        return net::udp_send_to(fd, target, serialize_md(message));
    };

    const auto send_data_snapshot = [&] {
        return send_snapshot_to(sender->fd, sender->target);
    };

    // 初始快照，接收端从 seq=1 开始同步
    send_data_snapshot();

    for (std::size_t i = 0; i < events; ++i) {
        // 处理接收端的快照请求
        sockaddr_in peer{};
        while (auto request = net::udp_recv_from(control, &peer)) {
            if (parse_snapshot_request(request->first)) {
                send_snapshot_to(control, peer);
            }
        }

        const Event event = generator.next();
        if (event.kind == Event::Kind::CancelOrder) {
            engine.cancel(event.cancel_id);
        } else {
            const auto result = engine.submit(event.order);
            for (const auto& trade : result.trades) {
                MdMessage message;
                message.type = MdMessage::Type::Trade;
                message.seq = next_seq++;
                message.buy_id = trade.buy_id;
                message.sell_id = trade.sell_id;
                message.price = trade.price;
                message.qty = trade.qty;

                if (drop_every != 0 && message.seq % drop_every == 0) {
                    ++dropped;  // 模拟丢包：序号仍推进
                    continue;
                }
                net::udp_send_to(sender->fd, sender->target,
                                 serialize_md(message));
                ++published;
            }
        }

        // 周期性快照，便于接收端在无请求时也能最终对齐；0 表示只发初始/最终快照
        if (snapshot_every != 0 && (i + 1) % snapshot_every == 0) {
            send_data_snapshot();
        }
        // 轻微限速，避免接收端 socket 缓冲溢出造成非预期丢包
        if ((i + 1) % 1000 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    send_data_snapshot();
    std::cout << "published events=" << events << " messages=" << published
              << " dropped=" << dropped << " final_seq=" << (next_seq - 1)
              << '\n';
    return 0;
}
