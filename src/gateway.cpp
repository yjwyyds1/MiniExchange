#include "exchange/gateway.hpp"

#include <cstdint>
#include <sstream>
#include <string>

#include "exchange/matching_engine.hpp"

namespace exchange {

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

}  // namespace

std::string apply_gateway_command(MatchingEngine& engine,
                                  std::string_view payload) {
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

}  // namespace exchange
