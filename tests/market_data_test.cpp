#include <iostream>
#include <stdexcept>
#include <string>

#include "exchange/market_data.hpp"

using namespace exchange;

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition))                                                      \
            throw std::runtime_error(std::string("failed: ") + #condition);    \
    } while (false)

namespace {

MdMessage snapshot(std::uint64_t seq, Price bid, Quantity bid_qty, Price ask,
                   Quantity ask_qty) {
    MdMessage message;
    message.type = MdMessage::Type::Snapshot;
    message.seq = seq;
    message.best_bid = bid;
    message.bid_qty = bid_qty;
    message.best_ask = ask;
    message.ask_qty = ask_qty;
    return message;
}

MdMessage trade(std::uint64_t seq, Price price, Quantity qty) {
    MdMessage message;
    message.type = MdMessage::Type::Trade;
    message.seq = seq;
    message.buy_id = 1;
    message.sell_id = 2;
    message.price = price;
    message.qty = qty;
    return message;
}

void test_codec_roundtrip() {
    const auto trade_message = trade(7, 10'000, 5);
    const auto parsed_trade = parse_md(serialize_md(trade_message));
    REQUIRE(parsed_trade.has_value());
    REQUIRE(parsed_trade->type == MdMessage::Type::Trade);
    REQUIRE(parsed_trade->seq == 7);
    REQUIRE(parsed_trade->price == 10'000);
    REQUIRE(parsed_trade->qty == 5);

    const auto snapshot_message = snapshot(8, 9'990, 10, 10'010, 20);
    const auto parsed_snapshot = parse_md(serialize_md(snapshot_message));
    REQUIRE(parsed_snapshot.has_value());
    REQUIRE(parsed_snapshot->type == MdMessage::Type::Snapshot);
    REQUIRE(parsed_snapshot->best_bid == 9'990);
    REQUIRE(parsed_snapshot->ask_qty == 20);

    REQUIRE(!parse_md("T 1 2").has_value());
    REQUIRE(!parse_md("X 1").has_value());
    REQUIRE(!parse_md("").has_value());
}

void test_request_codec() {
    REQUIRE(make_snapshot_request(42) == "REQ 42");
    REQUIRE(parse_snapshot_request("REQ 42") == 42u);
    REQUIRE(!parse_snapshot_request("REQ").has_value());
    REQUIRE(!parse_snapshot_request("T 42").has_value());
}

void test_normal_stream() {
    LocalBook book;
    REQUIRE(book.expected_seq() == 1);

    REQUIRE(book.on_message(snapshot(1, 9'990, 10, 10'010, 20)) ==
            LocalBook::Result::Rebuilt);
    REQUIRE(book.expected_seq() == 2);
    REQUIRE(!book.needs_snapshot());

    REQUIRE(book.on_message(trade(2, 10'000, 3)) == LocalBook::Result::Applied);
    REQUIRE(book.on_message(trade(3, 10'001, 4)) == LocalBook::Result::Applied);
    REQUIRE(book.expected_seq() == 4);
    REQUIRE(book.trades() == 2);
    REQUIRE(book.last_trade_price() == 10'001);
    REQUIRE(book.last_trade_qty() == 4);
    REQUIRE(book.gaps() == 0);
}

void test_gap_detection_and_rebuild() {
    LocalBook book;
    REQUIRE(book.on_message(snapshot(1, 9'990, 10, 10'010, 20)) ==
            LocalBook::Result::Rebuilt);
    REQUIRE(book.on_message(trade(2, 10'000, 3)) == LocalBook::Result::Applied);

    // 缺少 seq=3，直接收到 seq=4
    REQUIRE(book.on_message(trade(4, 10'002, 5)) ==
            LocalBook::Result::GapDetected);
    REQUIRE(book.needs_snapshot());
    REQUIRE(book.expected_seq() == 3);
    REQUIRE(book.gaps() == 1);
    REQUIRE(book.missing_messages() == 1);

    // 失步期间增量被忽略
    REQUIRE(book.on_message(trade(3, 10'001, 2)) ==
            LocalBook::Result::Ignored);
    REQUIRE(book.ignored() == 1);

    // 快照重建，expected 跳到快照 seq+1
    REQUIRE(book.on_message(snapshot(6, 10'005, 8, 10'015, 9)) ==
            LocalBook::Result::Rebuilt);
    REQUIRE(!book.needs_snapshot());
    REQUIRE(book.expected_seq() == 7);
    REQUIRE(book.best_bid() == 10'005);
    REQUIRE(book.ask_qty() == 9);
    REQUIRE(book.on_message(trade(7, 10'006, 1)) == LocalBook::Result::Applied);
    REQUIRE(book.expected_seq() == 8);
}

void test_stale_snapshot_ignored() {
    LocalBook book;
    REQUIRE(book.on_message(snapshot(5, 9'990, 10, 10'010, 20)) ==
            LocalBook::Result::Rebuilt);
    REQUIRE(book.best_bid() == 9'990);

    REQUIRE(book.on_message(snapshot(3, 1, 1, 2, 2)) ==
            LocalBook::Result::Duplicate);
    REQUIRE(book.best_bid() == 9'990);
    REQUIRE(book.duplicates() == 1);
    REQUIRE(book.expected_seq() == 6);
}

void test_duplicate_trade_ignored() {
    LocalBook book;
    REQUIRE(book.on_message(snapshot(1, 9'990, 10, 10'010, 20)) ==
            LocalBook::Result::Rebuilt);
    REQUIRE(book.on_message(trade(2, 10'000, 3)) == LocalBook::Result::Applied);
    REQUIRE(book.on_message(trade(2, 10'000, 3)) ==
            LocalBook::Result::Duplicate);
    REQUIRE(book.trades() == 1);
    REQUIRE(book.expected_seq() == 3);
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

    run("codec_roundtrip", test_codec_roundtrip);
    run("request_codec", test_request_codec);
    run("normal_stream", test_normal_stream);
    run("gap_detection_and_rebuild", test_gap_detection_and_rebuild);
    run("stale_snapshot_ignored", test_stale_snapshot_ignored);
    run("duplicate_trade_ignored", test_duplicate_trade_ignored);

    if (failed != 0) {
        std::cout << failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
