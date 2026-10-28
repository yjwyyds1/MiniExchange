#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "matching_engine.hpp"
#include "order_generator.hpp"

namespace exchange {

// 生成 count 条可复现事件
std::vector<Event> generate_events(OrderGenerator& generator, std::size_t count);

// 文本序列化：N <id> <B|S> <L|M> <price> <qty> / C <id>
std::string serialize_event(const Event& event);
std::optional<Event> parse_event(std::string_view line);

void write_events(const std::vector<Event>& events, std::ostream& out);
std::optional<std::vector<Event>> read_events(std::istream& in);

// 回放：逐条提交/撤销，输出成交（T ...）与最终快照（S ...）。
// 输出只依赖事件序列与引擎实现，不读墙钟、不依赖哈希遍历顺序。
void replay(const std::vector<Event>& events, MatchingEngine& engine, std::ostream& out);

}  // namespace exchange
