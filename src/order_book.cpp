#include "exchange/order_book.hpp"

#include <limits>
#include <stdexcept>

namespace exchange {

void PriceLevel::add(Quantity qty) {
    const Quantity old = low;
    low += qty;
    if (low < old) {
        if (high == std::numeric_limits<Quantity>::max())
            throw std::overflow_error("price level depth overflow");
        ++high;
    }
}

void PriceLevel::subtract(Quantity qty) {
    if (low < qty) {
        if (high == 0) throw std::logic_error("price level depth underflow");
        --high;
    }
    low -= qty;
}

Quantity PriceLevel::depth() const {
    return high == 0 ? low : std::numeric_limits<Quantity>::max();
}

bool OrderBook::insert(const std::shared_ptr<Order>& order) {
    if (!order || locations_.contains(order->id)) return false;
    auto& levels = order->side == Side::Buy ? bids_ : asks_;
    auto [level_it, created] = levels.try_emplace(order->price);
    (void)created;
    auto& level = level_it->second;
    level.add(order->qty);
    level.orders.push_back(order);
    locations_.emplace(order->id, Location{order->side, order->price,
                                          std::prev(level.orders.end())});
    ++count_;
    return true;
}

void OrderBook::reduce(const std::shared_ptr<Order>& order, Quantity filled_qty) {
    const auto location = locations_.find(order->id);
    if (location == locations_.end() ||
        location->second.iterator->get() != order.get() ||
        filled_qty > order->qty)
        throw std::invalid_argument("invalid resting order fill");
    auto& levels = order->side == Side::Buy ? bids_ : asks_;
    auto level = levels.find(order->price);
    if (level == levels.end()) throw std::logic_error("missing price level");
    level->second.subtract(filled_qty);
    order->qty -= filled_qty;
}

void OrderBook::remove(const std::shared_ptr<Order>& order) {
    if (!order) return;
    const auto location = locations_.find(order->id);
    if (location == locations_.end() || location->second.iterator->get() != order.get())
        return;
    auto& levels = order->side == Side::Buy ? bids_ : asks_;
    const auto level = levels.find(order->price);
    if (level == levels.end()) throw std::logic_error("missing price level");
    level->second.subtract(order->qty);
    level->second.orders.erase(location->second.iterator);
    locations_.erase(location);
    --count_;
    if (level->second.orders.empty()) levels.erase(level);
}

std::shared_ptr<Order> OrderBook::best(Side side) const {
    const auto& levels = side == Side::Buy ? bids_ : asks_;
    if (levels.empty()) return nullptr;
    const auto& queue = side == Side::Buy ? levels.rbegin()->second.orders
                                          : levels.begin()->second.orders;
    return queue.empty() ? nullptr : queue.front();
}

std::optional<Price> OrderBook::bestPrice(Side side) const {
    const auto& levels = side == Side::Buy ? bids_ : asks_;
    if (levels.empty()) return std::nullopt;
    return side == Side::Buy ? levels.rbegin()->first : levels.begin()->first;
}

Quantity OrderBook::depthAtBest(Side side) const {
    const auto& levels = side == Side::Buy ? bids_ : asks_;
    if (levels.empty()) return 0;
    return side == Side::Buy ? levels.rbegin()->second.depth() :
                               levels.begin()->second.depth();
}

bool OrderBook::empty(Side side) const {
    const auto& levels = side == Side::Buy ? bids_ : asks_;
    return levels.empty();
}

std::size_t OrderBook::orderCount() const { return count_; }

}  // namespace exchange
