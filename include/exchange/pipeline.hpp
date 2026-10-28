#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "matching_engine.hpp"
#include "order_generator.hpp"

namespace exchange {

// 流水线队列容量：2 的幂不是硬性要求（现有 SpscQueue 用取模），
// 但保持 2 的幂便于以后替换为位掩码索引实现。
inline constexpr std::size_t kPipelineCapacity = 1u << 14;

struct PipelineStats {
    std::size_t events = 0;
    std::size_t trades = 0;
    std::vector<std::uint64_t> latencies_ns;   // 入队 -> 处理完成
    std::vector<std::uint64_t> queue_wait_ns;  // 入队 -> 出队
    std::vector<std::uint64_t> service_ns;     // 出队 -> 处理完成
    Snapshot final_snapshot;
};

std::uint64_t now_ns();

// 单生产者（事件流）→ SpscQueue → 单消费者（撮合引擎）双线程流水线。
// 输入按值接收，调用方可 std::move 避免拷贝；撮合结果与单线程顺序处理一致。
PipelineStats run_pipeline(std::vector<Event> events, MatchingEngine& engine);

}  // namespace exchange
