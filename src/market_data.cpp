#include "exchange/market_data.hpp"

#include <sstream>

namespace exchange {

std::string serialize_md(const MdMessage& message) {
    std::ostringstream out;
    if (message.type == MdMessage::Type::Trade) {
        out << "T " << message.seq << ' ' << message.buy_id << ' '
            << message.sell_id << ' ' << message.price << ' ' << message.qty;
    } else {
        out << "S " << message.seq << ' ' << message.best_bid << ' '
            << message.bid_qty << ' ' << message.best_ask << ' '
            << message.ask_qty;
    }
    return out.str();
}

std::optional<MdMessage> parse_md(std::string_view line) {
    std::istringstream in{std::string(line)};
    char kind = 0;
    if (!(in >> kind)) return std::nullopt;

    MdMessage message;
    if (kind == 'T') {
        message.type = MdMessage::Type::Trade;
        if (!(in >> message.seq >> message.buy_id >> message.sell_id >>
              message.price >> message.qty)) {
            return std::nullopt;
        }
        return message;
    }
    if (kind == 'S') {
        message.type = MdMessage::Type::Snapshot;
        if (!(in >> message.seq >> message.best_bid >> message.bid_qty >>
              message.best_ask >> message.ask_qty)) {
            return std::nullopt;
        }
        return message;
    }
    return std::nullopt;
}

std::string make_snapshot_request(std::uint64_t expected_seq) {
    return "REQ " + std::to_string(expected_seq);
}

std::optional<std::uint64_t> parse_snapshot_request(std::string_view line) {
    std::istringstream in{std::string(line)};
    std::string kind;
    std::uint64_t expected = 0;
    if (!(in >> kind >> expected) || kind != "REQ") return std::nullopt;
    return expected;
}

LocalBook::Result LocalBook::on_message(const MdMessage& message) {
    // 过期/重复消息：直接丢弃
    if (message.seq < expected_) {
        ++duplicates_;
        return Result::Duplicate;
    }

    // 快照是权威状态：seq 足够新就重建，失步状态随之解除
    if (message.type == MdMessage::Type::Snapshot) {
        if (!out_of_sync_ && message.seq > expected_) {
            missing_messages_ += message.seq - expected_;
            ++gaps_;
        }
        best_bid_ = message.best_bid;
        bid_qty_ = message.bid_qty;
        best_ask_ = message.best_ask;
        ask_qty_ = message.ask_qty;
        expected_ = message.seq + 1;
        out_of_sync_ = false;
        ++rebuilds_;
        return Result::Rebuilt;
    }

    // 失步期间忽略增量，等待快照
    if (out_of_sync_) {
        ++ignored_;
        return Result::Ignored;
    }

    if (message.seq > expected_) {
        missing_messages_ += message.seq - expected_;
        ++gaps_;
        out_of_sync_ = true;
        return Result::GapDetected;
    }

    // seq == expected_
    last_trade_price_ = message.price;
    last_trade_qty_ = message.qty;
    ++trades_;
    ++expected_;
    return Result::Applied;
}

}  // namespace exchange
