#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include <unistd.h>

#include "exchange/net.hpp"
#include "exchange/order_generator.hpp"

using namespace exchange;

namespace {

void usage() {
    std::cout << "用法: sample_client [host] [port] [count] [seed]\n"
              << "默认: 127.0.0.1 9001 1000 42\n";
}

std::string command_for(const Event& event) {
    if (event.kind == Event::Kind::CancelOrder) {
        return "C " + std::to_string(event.cancel_id);
    }
    std::ostringstream out;
    out << "N " << event.order.id << ' '
        << (event.order.side == Side::Buy ? 'B' : 'S') << ' '
        << event.order.price << ' ' << event.order.qty;
    return out.str();
}

std::size_t count_trades(const std::string& response) {
    std::size_t trades = 0;
    std::istringstream in(response);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("T ", 0) == 0) ++trades;
    }
    return trades;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 5) {
        usage();
        return 1;
    }

    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    std::uint16_t port = 9001;
    std::size_t count = 1000;
    std::uint64_t seed = 42;
    try {
        if (argc > 2) port = static_cast<std::uint16_t>(std::stoi(argv[2]));
        if (argc > 3) count = static_cast<std::size_t>(std::stoull(argv[3]));
        if (argc > 4) seed = static_cast<std::uint64_t>(std::stoull(argv[4]));
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    const int fd = net::connect_to(host, port);
    if (fd < 0) {
        std::cerr << "连接 " << host << ':' << port << " 失败\n";
        return 1;
    }

    OrderGenerator generator(seed);
    std::size_t total_trades = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Event event = generator.next();
        const std::string command = command_for(event);
        if (!net::send_frame(fd, command)) {
            std::cerr << "发送失败（第 " << i << " 条）\n";
            break;
        }
        const auto response = net::recv_frame(fd);
        if (!response) {
            std::cerr << "接收响应失败（第 " << i << " 条）\n";
            break;
        }
        total_trades += count_trades(*response);
        if (i < 5) {
            std::cout << "> " << command << '\n' << *response;
        }
    }

    std::cout << "sent " << count << " events, received " << total_trades
              << " trades\n";
    ::close(fd);
    return 0;
}
