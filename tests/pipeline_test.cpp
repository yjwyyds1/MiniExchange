#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "exchange/order_generator.hpp"
#include "exchange/pipeline.hpp"
#include "exchange/replay.hpp"

using namespace exchange;

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition))                                                      \
            throw std::runtime_error(std::string("failed: ") + #condition);    \
    } while (false)

namespace {

// 流水线结果必须与单线程顺序处理完全一致
void test_pipeline_matches_single_threaded() {
    OrderGenerator generator(123);
    const auto events = generate_events(generator, 20'000);

    MatchingEngine direct;
    std::size_t direct_trades = 0;
    for (const auto& event : events) {
        if (event.kind == Event::Kind::CancelOrder) {
            direct.cancel(event.cancel_id);
        } else {
            direct_trades += direct.submit(event.order).trades.size();
        }
    }
    const auto direct_snapshot = direct.snapshot();

    auto pipeline_events = events;
    MatchingEngine piped;
    const auto stats = run_pipeline(std::move(pipeline_events), piped);

    REQUIRE(stats.events == events.size());
    REQUIRE(stats.latencies_ns.size() == events.size());
    REQUIRE(stats.queue_wait_ns.size() == events.size());
    REQUIRE(stats.service_ns.size() == events.size());
    REQUIRE(stats.trades == direct_trades);
    REQUIRE(stats.final_snapshot.seq == direct_snapshot.seq);
    REQUIRE(stats.final_snapshot.best_bid == direct_snapshot.best_bid);
    REQUIRE(stats.final_snapshot.bid_qty == direct_snapshot.bid_qty);
    REQUIRE(stats.final_snapshot.best_ask == direct_snapshot.best_ask);
    REQUIRE(stats.final_snapshot.ask_qty == direct_snapshot.ask_qty);
}

// 小规模边界：1 条与 0 条事件
void test_pipeline_boundaries() {
    MatchingEngine one_engine;
    std::vector<Event> one;
    Event event;
    event.kind = Event::Kind::NewOrder;
    event.order.id = 1;
    event.order.side = Side::Buy;
    event.order.type = OrderType::Limit;
    event.order.price = 100;
    event.order.qty = 3;
    one.push_back(event);
    const auto one_stats = run_pipeline(std::move(one), one_engine);
    REQUIRE(one_stats.events == 1);
    REQUIRE(one_stats.final_snapshot.best_bid == 100);

    MatchingEngine empty_engine;
    const auto empty_stats = run_pipeline({}, empty_engine);
    REQUIRE(empty_stats.events == 0);
    REQUIRE(empty_stats.latencies_ns.empty());
    REQUIRE(empty_stats.queue_wait_ns.empty());
    REQUIRE(empty_stats.service_ns.empty());
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

    run("pipeline_matches_single_threaded", test_pipeline_matches_single_threaded);
    run("pipeline_boundaries", test_pipeline_boundaries);

    if (failed != 0) {
        std::cout << failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
