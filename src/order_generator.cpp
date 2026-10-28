#include "exchange/order_generator.hpp"

namespace exchange {

OrderGenerator::OrderGenerator(std::uint64_t seed) : rng_(seed) {}

Event OrderGenerator::next() {
    static constexpr Price kMid = 10'000;  // 100.00，单位为 tick

    // 5% 撤单：从最近 ID 中挑一个，大概率命中仍挂在簿上的订单
    if (!recent_ids_.empty() && rng_() % 100 < 5) {
        Event e;
        e.kind = Event::Kind::CancelOrder;
        e.cancel_id = recent_ids_[rng_() % recent_ids_.size()];
        return e;
    }

    Event e;
    e.kind = Event::Kind::NewOrder;
    e.order.id = next_id_++;
    e.order.side = (rng_() % 2) ? Side::Buy : Side::Sell;
    e.order.type = (rng_() % 10 < 9) ? OrderType::Limit : OrderType::Market;

    const auto offset = static_cast<Price>(rng_() % 200) - 100;
    e.order.price = (e.order.type == OrderType::Market)
        ? 0
        : kMid + offset + (e.order.side == Side::Buy ? -20 : 20);
    e.order.qty = rng_() % 100 + 1;

    recent_ids_.push_back(e.order.id);
    if (recent_ids_.size() > 512) {
        recent_ids_.pop_front();
    }
    return e;
}

}  // namespace exchange
