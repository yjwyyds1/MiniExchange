#pragma once

#include <cstdint>
#include <deque>
#include <random>

#include "types.hpp"

namespace exchange {

// 事件：回放与网络传输的统一输入单元
struct Event {
    enum class Kind : std::uint8_t { NewOrder, CancelOrder };

    Kind    kind = Kind::NewOrder;
    Order   order;       // kind == NewOrder 时有效
    OrderId cancel_id = 0;  // kind == CancelOrder 时有效
};

// 可复现的订单流：同一 seed 生成完全相同的 Event 序列。
// 撤单从最近生成的 512 个 ID 中挑选，提升撤单命中活动订单的概率。
class OrderGenerator {
public:
    explicit OrderGenerator(std::uint64_t seed);

    Event next();

private:
    std::mt19937_64 rng_;
    OrderId next_id_ = 1;
    std::deque<OrderId> recent_ids_;
};

}  // namespace exchange
