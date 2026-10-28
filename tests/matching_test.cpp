#include "exchange/matching_engine.hpp"
#include <stdexcept>
#include <limits>
using namespace exchange;
#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error("failed: " #condition); } while (false)

int main() {
    MatchingEngine engine;
    REQUIRE(engine.submit(Order{1,Side::Sell,OrderType::Limit,100,2}).accepted);
    REQUIRE(engine.submit(Order{2,Side::Sell,OrderType::Limit,99,1}).accepted);
    const auto result = engine.submit(Order{3,Side::Buy,OrderType::Limit,101,2});
    REQUIRE(result.accepted && result.trades.size() == 2);
    REQUIRE(result.trades[0].sell_id == 2 && result.trades[0].price == 99);
    REQUIRE(result.trades[1].sell_id == 1 && result.trades[1].price == 100);
    REQUIRE(engine.snapshot().best_ask == 100 && engine.snapshot().ask_qty == 1);
    REQUIRE(engine.cancel(1));
    REQUIRE(engine.snapshot().ask_qty == 0);
    REQUIRE(engine.statusOf(1) == OrderStatus::Cancelled);
    REQUIRE(engine.statusOf(2) == OrderStatus::Filled);
    REQUIRE(engine.statusOf(3) == OrderStatus::Filled);
    const auto reused = engine.submit(Order{2,Side::Sell,OrderType::Limit,100,1});
    REQUIRE(!reused.accepted && reused.error == ErrorCode::DuplicateOrderId);
    MatchingEngine large;
    REQUIRE(large.submit(Order{10,Side::Sell,OrderType::Limit,100,
                               std::numeric_limits<Quantity>::max()}).accepted);
    REQUIRE(large.submit(Order{11,Side::Sell,OrderType::Limit,100,1}).accepted);
    REQUIRE(large.snapshot().ask_qty == std::numeric_limits<Quantity>::max());
    REQUIRE(large.cancel(10));
    REQUIRE(large.snapshot().ask_qty == 1);

    OrderBook direct;
    auto resting = std::make_shared<Order>(Order{20,Side::Sell,OrderType::Limit,100,5});
    REQUIRE(direct.insert(resting));
    direct.reduce(resting, 2);
    REQUIRE(resting->qty == 3 && direct.depthAtBest(Side::Sell) == 3);

    MatchingEngine market;
    const auto empty = market.submit(Order{1,Side::Buy,OrderType::Market,0,5});
    REQUIRE(empty.accepted && empty.error == ErrorCode::Ok);
    REQUIRE(empty.status == OrderStatus::Cancelled && empty.unfilled_qty == 5);
    REQUIRE(empty.trades.empty() && market.statusOf(1) == OrderStatus::Cancelled);
    REQUIRE(market.submit(Order{2,Side::Sell,OrderType::Limit,100,2}).accepted);
    const auto partial = market.submit(Order{3,Side::Buy,OrderType::Market,0,5});
    REQUIRE(partial.accepted && partial.error == ErrorCode::Ok);
    REQUIRE(partial.status == OrderStatus::Cancelled && partial.unfilled_qty == 3);
    REQUIRE(partial.trades.size() == 1 && partial.trades[0].qty == 2);
    REQUIRE(market.statusOf(2) == OrderStatus::Filled);
    REQUIRE(market.submit(Order{4,Side::Sell,OrderType::Limit,100,2}).accepted);
    const auto full = market.submit(Order{5,Side::Buy,OrderType::Market,0,2});
    REQUIRE(full.accepted && full.error == ErrorCode::Ok);
    REQUIRE(full.status == OrderStatus::Filled && full.unfilled_qty == 0);
    REQUIRE(!market.submit(Order{4,Side::Buy,OrderType::Limit,100,1}).accepted);

    MatchingEngine bounded;
    for (OrderId id = 1; id <= 4097; ++id)
        REQUIRE(bounded.submit(Order{id,Side::Buy,OrderType::Market,0,1}).accepted);
    REQUIRE(!bounded.statusOf(1).has_value());
    REQUIRE(bounded.statusOf(4097) == OrderStatus::Cancelled);
}
