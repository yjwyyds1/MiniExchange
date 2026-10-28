#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <unistd.h>

#include "exchange/matching_engine.hpp"
#include "exchange/net.hpp"

using namespace exchange;

namespace {

const char* error_name(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:               return "OK";
        case ErrorCode::InvalidOrderId:   return "BAD_ID";
        case ErrorCode::DuplicateOrderId: return "DUP_ID";
        case ErrorCode::UnknownOrderId:   return "UNKNOWN_ID";
        case ErrorCode::InvalidPrice:     return "BAD_PRICE";
        case ErrorCode::InvalidQuantity:  return "BAD_QTY";
    }
    return "UNKNOWN";
}

const char* status_name(OrderStatus status) {
    switch (status) {
        case OrderStatus::New:           return "New";
        case OrderStatus::PartialFilled: return "PartialFilled";
        case OrderStatus::Filled:        return "Filled";
        case OrderStatus::Cancelled:     return "Cancelled";
        case OrderStatus::Rejected:      return "Rejected";
    }
    return "Unknown";
}

std::string snapshot_line(const Snapshot& snapshot) {
    std::ostringstream out;
    out << "Q " << snapshot.best_bid << ' ' << snapshot.bid_qty << ' '
        << snapshot.best_ask << ' ' << snapshot.ask_qty << ' ' << snapshot.seq
        << '\n';
    return out.str();
}

// 单帧 payload = 一条命令；响应帧 payload 可包含多行
std::string apply_command(MatchingEngine& engine, std::string_view payload) {
    std::istringstream in{std::string(payload)};
    char command = 0;
    if (!(in >> command)) return "E BAD_CMD\n";

    if (command == 'N') {
        OrderId id = 0;
        char side = 0;
        std::int64_t price = 0;
        Quantity qty = 0;
        if (!(in >> id >> side >> price >> qty) || (side != 'B' && side != 'S')) {
            return "E BAD_ARGS\n";
        }

        Order order;
        order.id = id;
        order.side = side == 'B' ? Side::Buy : Side::Sell;
        order.type = price == 0 ? OrderType::Market : OrderType::Limit;
        order.price = static_cast<Price>(price);
        order.qty = qty;

        const auto result = engine.submit(order);
        if (!result.accepted) {
            std::string out = "R REJ ";
            out += error_name(result.error);
            out += '\n';
            return out;
        }

        std::ostringstream out;
        out << "R OK\n";
        for (const auto& trade : result.trades) {
            out << "T " << trade.buy_id << ' ' << trade.sell_id << ' '
                << trade.price << ' ' << trade.qty << ' ' << trade.seq << '\n';
        }
        out << snapshot_line(engine.snapshot());
        return out.str();
    }

    if (command == 'C') {
        OrderId id = 0;
        if (!(in >> id)) return "E BAD_ARGS\n";
        return engine.cancel(id) ? "R OK\n" : "R REJ UNKNOWN_ID\n";
    }

    if (command == 'S') {
        OrderId id = 0;
        if (!(in >> id)) return "E BAD_ARGS\n";
        const auto status = engine.statusOf(id);
        if (!status) return "S NONE\n";
        return std::string("S ") + status_name(*status) + "\n";
    }

    if (command == 'Q') {
        return snapshot_line(engine.snapshot());
    }

    return "E BAD_CMD\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 9001;
    try {
        if (argc > 1) port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    } catch (const std::exception& e) {
        std::cerr << "端口参数错误: " << e.what() << '\n';
        return 1;
    }

    const int listener = net::make_listener(port);
    if (listener < 0) {
        std::cerr << "监听失败: " << std::strerror(errno) << '\n';
        return 1;
    }
    std::cout << "listening on 127.0.0.1:" << port << '\n' << std::flush;

    MatchingEngine engine;
    for (;;) {
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::cerr << "accept 失败: " << std::strerror(errno) << '\n';
            break;
        }
        std::cout << "client connected\n" << std::flush;

        for (;;) {
            const auto frame = net::recv_frame(client);
            if (!frame) break;  // 对端关闭或协议错误
            const auto response = apply_command(engine, *frame);
            if (!net::send_frame(client, response)) break;
        }

        ::close(client);
        std::cout << "client disconnected\n" << std::flush;
    }

    ::close(listener);
    return 0;
}
