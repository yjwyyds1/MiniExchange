#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "exchange/order_generator.hpp"
#include "exchange/replay.hpp"

using namespace exchange;

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition))                                                      \
            throw std::runtime_error(std::string("failed: ") + #condition);    \
    } while (false)

namespace {

// 同一 seed 生成完全相同的事件序列
void test_same_seed_same_events() {
    OrderGenerator first(42);
    OrderGenerator second(42);

    const auto a = generate_events(first, 1000);
    const auto b = generate_events(second, 1000);
    REQUIRE(a.size() == b.size());

    std::ostringstream out_a;
    std::ostringstream out_b;
    write_events(a, out_a);
    write_events(b, out_b);
    REQUIRE(out_a.str() == out_b.str());
}

// 序列化 -> 解析 -> 再序列化，文本必须完全一致
void test_event_roundtrip() {
    OrderGenerator generator(7);
    const auto events = generate_events(generator, 500);

    std::ostringstream first;
    write_events(events, first);

    std::istringstream input(first.str());
    const auto parsed = read_events(input);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == events.size());

    std::ostringstream second;
    write_events(*parsed, second);
    REQUIRE(first.str() == second.str());
}

// 同一批事件回放两次，输出逐字节一致
void test_replay_deterministic() {
    OrderGenerator generator(42);
    const auto events = generate_events(generator, 100000);

    std::ostringstream first;
    std::ostringstream second;
    MatchingEngine engine_a;
    MatchingEngine engine_b;
    replay(events, engine_a, first);
    replay(events, engine_b, second);

    REQUIRE(first.str() == second.str());
    REQUIRE(first.str().find("T ") != std::string::npos);
}

// 格式错误的事件行必须被拒绝
void test_malformed_event_rejected() {
    REQUIRE(!parse_event("N 1 B L 100").has_value());
    REQUIRE(!parse_event("N 1 X L 100 5").has_value());
    REQUIRE(!parse_event("Z 1").has_value());
    REQUIRE(!parse_event("C").has_value());
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

    run("same_seed_same_events", test_same_seed_same_events);
    run("event_roundtrip", test_event_roundtrip);
    run("replay_deterministic", test_replay_deterministic);
    run("malformed_event_rejected", test_malformed_event_rejected);

    if (failed != 0) {
        std::cout << failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
