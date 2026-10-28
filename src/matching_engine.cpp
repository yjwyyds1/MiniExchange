#include "exchange/matching_engine.hpp"

#include <algorithm>

namespace exchange {

void MatchingEngine::reserve(std::size_t expected_active_orders) {
    orders_.reserve(expected_active_orders);
}

std::vector<Trade> MatchingEngine::match(Order& taker) {
    std::vector<Trade> trades;

    while (taker.qty > 0) {
        // 1. 取对手方最优价上的最优先订单（价格优先 + 时间优先都由簿保证）
        const Side opp = (taker.side == Side::Buy) ? Side::Sell : Side::Buy;
        const auto resting = book_.best(opp);
        if (!resting) break;  // 对手方无单，无法成交

        // 2. 限价单检查价格是否触及（市价单跳过价格检查，直接成交）
        if (taker.type == OrderType::Limit) {
            const bool crossed = (taker.side == Side::Buy)
                ? (resting->price <= taker.price)      // 买价 >= 最低卖价
                : (resting->price >= taker.price);     // 卖价 <= 最高买价
            if (!crossed) break;                        // 未触及，剩余挂单
        }

        // 3. 按挂单价成交 min(双方剩余数量)
        const Quantity qty = std::min(taker.qty, resting->qty);
        const auto [buy_id, sell_id] = (taker.side == Side::Buy)
            ? std::pair{taker.id, resting->id}
            : std::pair{resting->id, taker.id};
        trades.push_back(Trade{buy_id, sell_id, resting->price, qty, next_seq_++});

        taker.qty -= qty;
        book_.reduce(resting, qty);

        // 4. 挂单被吃光：移出订单簿，标记 Filled，移出索引
        if (resting->qty == 0) {
            book_.remove(resting);
            resting->status = OrderStatus::Filled;
            eraseIndex(resting->id);
        } else {
            resting->status = OrderStatus::PartialFilled;
        }
    }
    return trades;
}

MatchingEngine::SubmitResult MatchingEngine::submit(Order order) {
    SubmitResult result;

    // ---- 校验（在分配 seq 之前失败 = 拒绝，不留下任何状态）----
    if (order.qty == 0) { result.error = ErrorCode::InvalidQuantity; return result; }
    if (order.type == OrderType::Limit && order.price <= 0) {
        result.error = ErrorCode::InvalidPrice; return result;
    }
    if (order.id == 0) {
        result.error = ErrorCode::InvalidOrderId; return result;
    }
    if (order.id <= last_id_) {
        result.error = ErrorCode::DuplicateOrderId; return result;
    }

    // ---- 到达：分配序号，登记索引 ----
    order.seq = next_seq_++;
    last_id_ = order.id;
    auto sp = std::make_shared<Order>(order);
    orders_.emplace(order.id, sp);

    // ---- 撮合 ----
    result.trades = match(*sp);

    // ---- 收尾：按剩余数量决定去向 ----
    if (sp->qty > 0) {
        if (sp->type == OrderType::Market) {
            // 市价单按 IOC 语义：未成交部分直接撤销（真实交易所常见做法）
            sp->status = OrderStatus::Cancelled;
            eraseIndex(sp->id);
            // IOC 未成交不是校验失败；accepted 仅表示订单进入了撮合。
        } else {
            sp->status = result.trades.empty() ? OrderStatus::New
                                               : OrderStatus::PartialFilled;
            book_.insert(sp);
        }
    } else {
        sp->status = OrderStatus::Filled;
        eraseIndex(sp->id);
    }
    result.accepted = true;
    result.status = sp->status;
    result.unfilled_qty = sp->qty;
    return result;
}

bool MatchingEngine::cancel(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return false;  // 不存在或已终结
    auto& order = it->second;
    book_.remove(order);            // 若仍在簿内则移除
    order->status = OrderStatus::Cancelled;
    eraseIndex(id);
    return true;
}

std::optional<OrderStatus> MatchingEngine::statusOf(OrderId id) const {
    const auto it = orders_.find(id);
    if (it != orders_.end()) return it->second->status;

    // 终态窗口是固定大小环形缓冲：从最新写入位置反向扫描。
    for (std::size_t i = 0; i < terminal_count_; ++i) {
        const std::size_t index =
            (terminal_next_ + kTerminalLimit - 1 - i) % kTerminalLimit;
        if (terminal_[index].id == id) return terminal_[index].status;
    }
    return std::nullopt;
}

Snapshot MatchingEngine::snapshot() const {
    Snapshot s;
    s.seq = next_seq_;
    s.best_bid = book_.bestPrice(Side::Buy).value_or(0);
    s.best_ask = book_.bestPrice(Side::Sell).value_or(0);
    s.bid_qty = book_.depthAtBest(Side::Buy);
    s.ask_qty = book_.depthAtBest(Side::Sell);
    return s;
}

void MatchingEngine::eraseIndex(OrderId id) {
    const auto it = orders_.find(id);
    if (it == orders_.end()) return;
    terminal_[terminal_next_] = TerminalEntry{id, it->second->status};
    terminal_next_ = (terminal_next_ + 1) % kTerminalLimit;
    if (terminal_count_ < kTerminalLimit) ++terminal_count_;
    orders_.erase(it);
}

}  // namespace exchange
