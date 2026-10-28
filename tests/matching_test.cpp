#include "exchange/matching_engine.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using namespace exchange;

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition))                                                      \
            throw std::runtime_error(std::string("failed: ") + #condition);    \
    } while (false)

namespace {

Order make_order(OrderId id, Side side, OrderType type, Price price, Quantity qty) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = type;
    o.price = price;
    o.qty = qty;
    return o;
}

// 卖盘价格优先：更优（更低）卖价先成交
void test_price_priority_ask_side() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 100, 2)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Sell, OrderType::Limit, 99, 1)).accepted);

    const auto r = e.submit(make_order(3, Side::Buy, OrderType::Limit, 101, 2));
    REQUIRE(r.accepted);
    REQUIRE(r.trades.size() == 2);
    REQUIRE(r.trades[0].buy_id == 3u && r.trades[0].sell_id == 2u);
    REQUIRE(r.trades[0].price == 99 && r.trades[0].qty == 1u);
    REQUIRE(r.trades[1].sell_id == 1u && r.trades[1].price == 100);
    REQUIRE(r.trades[1].qty == 1u);

    const auto s = e.snapshot();
    REQUIRE(s.best_ask == 100 && s.ask_qty == 1u);
}

// 买盘价格优先：更优（更高）买价先成交
void test_price_priority_bid_side() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Buy, OrderType::Limit, 99, 1)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Buy, OrderType::Limit, 100, 1)).accepted);

    const auto r = e.submit(make_order(3, Side::Sell, OrderType::Limit, 99, 1));
    REQUIRE(r.trades.size() == 1);
    REQUIRE(r.trades[0].buy_id == 2u && r.trades[0].sell_id == 3u);
    REQUIRE(r.trades[0].price == 100 && r.trades[0].qty == 1u);
    REQUIRE(e.statusOf(2) == OrderStatus::Filled);

    const auto s = e.snapshot();
    REQUIRE(s.best_bid == 99 && s.bid_qty == 1u);
}

// 同价时间优先（FIFO）：先到的先成交，后到的部分成交留簿
void test_time_priority_same_price() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 100, 2)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Sell, OrderType::Limit, 100, 3)).accepted);

    const auto r = e.submit(make_order(3, Side::Buy, OrderType::Limit, 100, 4));
    REQUIRE(r.trades.size() == 2);
    REQUIRE(r.trades[0].sell_id == 1u && r.trades[0].qty == 2u);
    REQUIRE(r.trades[1].sell_id == 2u && r.trades[1].qty == 2u);
    REQUIRE(e.statusOf(2) == OrderStatus::PartialFilled);
    REQUIRE(e.snapshot().ask_qty == 1u);
}

// 卖单吃单后剩余数量挂簿（taker 部分成交）
void test_sell_taker_partial_fill_rests() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Buy, OrderType::Limit, 100, 3)).accepted);

    const auto r = e.submit(make_order(2, Side::Sell, OrderType::Limit, 100, 5));
    REQUIRE(r.accepted);
    REQUIRE(r.status == OrderStatus::PartialFilled);
    REQUIRE(r.unfilled_qty == 2u);
    REQUIRE(r.trades.size() == 1 && r.trades[0].qty == 3u);

    const auto s = e.snapshot();
    REQUIRE(s.best_bid == 0 && s.bid_qty == 0u);
    REQUIRE(s.best_ask == 100 && s.ask_qty == 2u);
}

// 限价未触及：不成交，整单挂簿
void test_limit_not_crossed() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 101, 2)).accepted);

    const auto r = e.submit(make_order(2, Side::Buy, OrderType::Limit, 100, 3));
    REQUIRE(r.accepted);
    REQUIRE(r.trades.empty());
    REQUIRE(r.status == OrderStatus::New);
    REQUIRE(r.unfilled_qty == 3u);

    const auto s = e.snapshot();
    REQUIRE(s.best_bid == 100 && s.bid_qty == 3u);
    REQUIRE(s.best_ask == 101 && s.ask_qty == 2u);
}

// 市价买单扫多档：成交价按簿内档位依次抬升
void test_market_sweeps_multiple_levels() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 100, 2)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Sell, OrderType::Limit, 101, 3)).accepted);

    const auto r = e.submit(make_order(3, Side::Buy, OrderType::Market, 0, 4));
    REQUIRE(r.accepted);
    REQUIRE(r.status == OrderStatus::Filled);
    REQUIRE(r.unfilled_qty == 0u);
    REQUIRE(r.trades.size() == 2);
    REQUIRE(r.trades[0].price == 100 && r.trades[0].qty == 2u);
    REQUIRE(r.trades[1].price == 101 && r.trades[1].qty == 2u);

    const auto s = e.snapshot();
    REQUIRE(s.best_ask == 101 && s.ask_qty == 1u);
}

// 市价卖单方向对称
void test_market_sell_side() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Buy, OrderType::Limit, 100, 2)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Buy, OrderType::Limit, 99, 1)).accepted);

    const auto r = e.submit(make_order(3, Side::Sell, OrderType::Market, 0, 2));
    REQUIRE(r.status == OrderStatus::Filled);
    REQUIRE(r.trades.size() == 1);
    REQUIRE(r.trades[0].buy_id == 1u && r.trades[0].sell_id == 3u);
    REQUIRE(r.trades[0].price == 100 && r.trades[0].qty == 2u);

    const auto s = e.snapshot();
    REQUIRE(s.best_bid == 99 && s.bid_qty == 1u);
}

// 撤单：活动单成功；不存在/已终结/重复撤销失败
void test_cancel_boundaries() {
    MatchingEngine e;
    REQUIRE(!e.cancel(999));

    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 100, 5)).accepted);
    REQUIRE(e.cancel(1));
    REQUIRE(e.statusOf(1) == OrderStatus::Cancelled);
    REQUIRE(e.snapshot().best_ask == 0 && e.snapshot().ask_qty == 0u);
    REQUIRE(!e.cancel(1));

    MatchingEngine f;
    REQUIRE(f.submit(make_order(1, Side::Sell, OrderType::Limit, 100, 1)).accepted);
    REQUIRE(f.submit(make_order(2, Side::Buy, OrderType::Limit, 100, 1)).accepted);
    REQUIRE(!f.cancel(1));
    REQUIRE(!f.cancel(2));
    REQUIRE(f.statusOf(1) == OrderStatus::Filled);
    REQUIRE(f.statusOf(2) == OrderStatus::Filled);
}

// 参数校验：非法 ID/价格/数量；被拒不推进 ID 高水位
void test_invalid_inputs() {
    MatchingEngine e;

    const auto id0 = e.submit(make_order(0, Side::Buy, OrderType::Limit, 100, 1));
    REQUIRE(!id0.accepted && id0.error == ErrorCode::InvalidOrderId);

    const auto bad_price = e.submit(make_order(1, Side::Buy, OrderType::Limit, 0, 1));
    REQUIRE(!bad_price.accepted && bad_price.error == ErrorCode::InvalidPrice);

    const auto neg_price = e.submit(make_order(2, Side::Buy, OrderType::Limit, -5, 1));
    REQUIRE(!neg_price.accepted && neg_price.error == ErrorCode::InvalidPrice);

    const auto bad_qty = e.submit(make_order(3, Side::Buy, OrderType::Limit, 100, 0));
    REQUIRE(!bad_qty.accepted && bad_qty.error == ErrorCode::InvalidQuantity);

    // 之前 id=1 因非法价格被拒，last_id_ 仍为 0，修正后同 ID 可重试
    const auto retry = e.submit(make_order(1, Side::Buy, OrderType::Limit, 100, 1));
    REQUIRE(retry.accepted && retry.status == OrderStatus::New);
}

// 空簿快照
void test_empty_snapshot() {
    MatchingEngine e;
    const auto s = e.snapshot();
    REQUIRE(s.best_bid == 0 && s.best_ask == 0);
    REQUIRE(s.bid_qty == 0u && s.ask_qty == 0u);
    REQUIRE(s.seq == 0u);
}

// 价格档全部成交后，档位被删除而不是留一个空档
void test_level_removed_when_empty() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 100, 1)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Buy, OrderType::Limit, 100, 1)).accepted);

    const auto s = e.snapshot();
    REQUIRE(s.best_ask == 0 && s.ask_qty == 0u);
}

// 双边快照与序号
void test_two_sided_snapshot_and_seq() {
    MatchingEngine e;
    REQUIRE(e.submit(make_order(1, Side::Buy, OrderType::Limit, 99, 2)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Buy, OrderType::Limit, 100, 3)).accepted);
    REQUIRE(e.submit(make_order(3, Side::Sell, OrderType::Limit, 101, 1)).accepted);
    REQUIRE(e.submit(make_order(4, Side::Sell, OrderType::Limit, 102, 4)).accepted);

    const auto s = e.snapshot();
    REQUIRE(s.best_bid == 100 && s.bid_qty == 3u);
    REQUIRE(s.best_ask == 101 && s.ask_qty == 1u);
    REQUIRE(s.seq == 4u);
}

// 档位总量超过 uint64_t 时饱和显示
void test_quantity_saturation() {
    MatchingEngine e;
    constexpr auto kMax = std::numeric_limits<Quantity>::max();
    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 100, kMax)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Sell, OrderType::Limit, 100, 1)).accepted);
    REQUIRE(e.snapshot().ask_qty == kMax);

    REQUIRE(e.cancel(1));
    REQUIRE(e.snapshot().ask_qty == 1u);
}

// 终态窗口：最近 4096 个终态可查，更早的被淘汰
void test_terminal_window() {
    MatchingEngine e;
    for (OrderId id = 1; id <= 4097; ++id) {
        REQUIRE(e.submit(make_order(id, Side::Buy, OrderType::Market, 0, 1)).accepted);
    }
    REQUIRE(!e.statusOf(1).has_value());
    REQUIRE(e.statusOf(4097) == OrderStatus::Cancelled);
}

// OrderBook::reduce 单独验证
void test_order_book_reduce() {
    OrderBook direct;
    auto resting = std::make_shared<Order>(
        make_order(20, Side::Sell, OrderType::Limit, 100, 5));
    REQUIRE(direct.insert(resting));

    direct.reduce(resting, 2);
    REQUIRE(resting->qty == 3u);
    REQUIRE(direct.depthAtBest(Side::Sell) == 3u);
}

// 把 orders.txt 中受理的事件按顺序重放，验证整链路成交序列
void test_manual_orders_sequence() {
    MatchingEngine e;

    REQUIRE(e.submit(make_order(1, Side::Sell, OrderType::Limit, 100, 2)).accepted);
    REQUIRE(e.submit(make_order(2, Side::Sell, OrderType::Limit, 100, 3)).accepted);

    const auto t3 = e.submit(make_order(3, Side::Buy, OrderType::Limit, 100, 4));
    REQUIRE(t3.trades.size() == 2);
    REQUIRE(t3.trades[0].seq == 3u && t3.trades[1].seq == 4u);

    const auto t4 = e.submit(make_order(4, Side::Buy, OrderType::Market, 0, 2));
    REQUIRE(t4.status == OrderStatus::Cancelled && t4.unfilled_qty == 1u);
    REQUIRE(t4.trades.size() == 1 && t4.trades[0].seq == 6u);

    const auto t5 = e.submit(make_order(5, Side::Buy, OrderType::Market, 0, 5));
    REQUIRE(t5.status == OrderStatus::Cancelled && t5.unfilled_qty == 5u);

    REQUIRE(e.submit(make_order(6, Side::Sell, OrderType::Limit, 99, 10)).accepted);
    REQUIRE(e.cancel(6));
    REQUIRE(!e.cancel(6));
    REQUIRE(!e.cancel(3));

    REQUIRE(e.submit(make_order(7, Side::Sell, OrderType::Limit, 100, 0)).error ==
            ErrorCode::InvalidQuantity);
    REQUIRE(e.submit(make_order(8, Side::Buy, OrderType::Limit, -5, 3)).error ==
            ErrorCode::InvalidPrice);
    REQUIRE(e.submit(make_order(4, Side::Buy, OrderType::Limit, 100, 1)).error ==
            ErrorCode::DuplicateOrderId);

    REQUIRE(e.submit(make_order(7, Side::Sell, OrderType::Limit, 100, 5)).accepted);
    REQUIRE(e.submit(make_order(8, Side::Sell, OrderType::Limit, 101, 2)).accepted);

    const auto t9 = e.submit(make_order(9, Side::Buy, OrderType::Limit, 101, 7));
    REQUIRE(t9.trades.size() == 2);
    REQUIRE(t9.trades[0].seq == 12u && t9.trades[1].seq == 13u);

    REQUIRE(e.submit(make_order(10, Side::Buy, OrderType::Limit, 100, 1)).accepted);
    const auto t11 = e.submit(make_order(11, Side::Sell, OrderType::Limit, 100, 1));
    REQUIRE(t11.trades.size() == 1 && t11.trades[0].seq == 16u);

    REQUIRE(e.statusOf(1) == OrderStatus::Filled);
    REQUIRE(e.statusOf(7) == OrderStatus::Filled);
    REQUIRE(e.statusOf(8) == OrderStatus::Filled);
}

}  // namespace

int main() {
    int failed = 0;
    const auto run = [&](const char* name, void (*fn)()) {
        try {
            fn();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << name << ": " << e.what() << '\n';
        }
    };

    run("price_priority_ask_side", test_price_priority_ask_side);
    run("price_priority_bid_side", test_price_priority_bid_side);
    run("time_priority_same_price", test_time_priority_same_price);
    run("sell_taker_partial_fill_rests", test_sell_taker_partial_fill_rests);
    run("limit_not_crossed", test_limit_not_crossed);
    run("market_sweeps_multiple_levels", test_market_sweeps_multiple_levels);
    run("market_sell_side", test_market_sell_side);
    run("cancel_boundaries", test_cancel_boundaries);
    run("invalid_inputs", test_invalid_inputs);
    run("empty_snapshot", test_empty_snapshot);
    run("level_removed_when_empty", test_level_removed_when_empty);
    run("two_sided_snapshot_and_seq", test_two_sided_snapshot_and_seq);
    run("quantity_saturation", test_quantity_saturation);
    run("terminal_window", test_terminal_window);
    run("order_book_reduce", test_order_book_reduce);
    run("manual_orders_sequence", test_manual_orders_sequence);

    if (failed != 0) {
        std::cout << failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
