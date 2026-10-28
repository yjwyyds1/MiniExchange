#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "types.hpp"

namespace exchange {

// 行情消息：成交（增量）与快照（全量最优一档），共用单调 seq
struct MdMessage {
    enum class Type : std::uint8_t { Trade, Snapshot };

    Type type = Type::Trade;
    std::uint64_t seq = 0;

    // Type::Trade
    OrderId buy_id = 0;
    OrderId sell_id = 0;
    Price price = 0;
    Quantity qty = 0;

    // Type::Snapshot
    Price best_bid = 0;
    Quantity bid_qty = 0;
    Price best_ask = 0;
    Quantity ask_qty = 0;
};

std::string serialize_md(const MdMessage& message);
std::optional<MdMessage> parse_md(std::string_view line);

std::string make_snapshot_request(std::uint64_t expected_seq);
std::optional<std::uint64_t> parse_snapshot_request(std::string_view line);

// 接收端本地簿与序号状态机（纯逻辑，不依赖 socket，便于测试）
class LocalBook {
public:
    enum class Result {
        Applied,      // 成交按序应用
        Duplicate,    // seq < expected：重复或过期
        GapDetected,  // seq > expected：发现缺口，需要请求快照
        Rebuilt,      // 收到可用快照并完成重建
        Ignored       // 失步期间忽略增量，等待快照
    };

    Result on_message(const MdMessage& message);

    bool needs_snapshot() const { return out_of_sync_; }
    std::uint64_t expected_seq() const { return expected_; }

    Price best_bid() const { return best_bid_; }
    Quantity bid_qty() const { return bid_qty_; }
    Price best_ask() const { return best_ask_; }
    Quantity ask_qty() const { return ask_qty_; }
    Price last_trade_price() const { return last_trade_price_; }
    Quantity last_trade_qty() const { return last_trade_qty_; }

    std::uint64_t trades() const { return trades_; }
    std::uint64_t duplicates() const { return duplicates_; }
    std::uint64_t gaps() const { return gaps_; }
    std::uint64_t missing_messages() const { return missing_messages_; }
    std::uint64_t rebuilds() const { return rebuilds_; }
    std::uint64_t ignored() const { return ignored_; }

private:
    std::uint64_t expected_ = 1;  // 发布端从 seq=1 开始
    bool out_of_sync_ = false;

    Price best_bid_ = 0;
    Quantity bid_qty_ = 0;
    Price best_ask_ = 0;
    Quantity ask_qty_ = 0;
    Price last_trade_price_ = 0;
    Quantity last_trade_qty_ = 0;

    std::uint64_t trades_ = 0;
    std::uint64_t duplicates_ = 0;
    std::uint64_t gaps_ = 0;
    std::uint64_t missing_messages_ = 0;
    std::uint64_t rebuilds_ = 0;
    std::uint64_t ignored_ = 0;
};

}  // namespace exchange
