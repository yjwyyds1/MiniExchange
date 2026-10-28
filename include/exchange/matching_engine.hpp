#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <unordered_map>
#include <vector>

#include "order_book.hpp"
#include "types.hpp"

namespace exchange {

    // 撮合引擎：单线程模型。校验、撮合、订单生命周期管理都在这里。
    class MatchingEngine {
        public:
            // use_pool=true 时，订单对象与控制块从 pmr 对象池分配；
            // 默认 false：本机实测 PMR 池慢于 malloc 基线，保留为可选实验。
            explicit MatchingEngine(bool use_pool = false);

            MatchingEngine(MatchingEngine&&) noexcept = default;
            MatchingEngine& operator=(MatchingEngine&& other);
            MatchingEngine(const MatchingEngine&) = delete;
            MatchingEngine& operator=(const MatchingEngine&) = delete;

            struct SubmitResult {
                bool               accepted = false;
                ErrorCode          error = ErrorCode::Ok;
                OrderStatus        status = OrderStatus::Rejected;
                Quantity           unfilled_qty = 0; // 挂簿或 IOC 撤销的剩余量
                std::vector<Trade> trades;   // 本次提交产生的成交（可能多笔多价）
            };

            // 按值接收订单（小对象值语义）：引擎统一分配 seq，保证时间优先的权威性
            SubmitResult submit(Order order);

            // 撤销：true=成功；订单不存在或已终结返回 false
            bool cancel(OrderId id);

            // 活动单和最近 4096 个终态单可查询；更早的终态需查外部日志。
            std::optional<OrderStatus> statusOf(OrderId id) const;
            Snapshot snapshot() const;                              // 顶层行情快照
            const OrderBook& book() const { return book_; }

            // 可选优化：预分配活动单索引桶，减少热路径中的 rehash 抖动。
            void reserve(std::size_t expected_active_orders);

        private:
            std::vector<Trade> match(Order& taker);  // 与对手方逐档撮合
            void eraseIndex(OrderId id);             // 订单终结时从索引移除

            // 声明在 book_/orders_ 之前：析构时最后销毁，保证订单归还到有效池。
            std::unique_ptr<std::pmr::unsynchronized_pool_resource> pool_;
            bool use_pool_ = false;
            OrderBook book_;
            // 订单索引：id -> 订单。簿与索引各持一份 shared_ptr，生命周期安全
            std::unordered_map<OrderId, std::shared_ptr<Order>> orders_;
            // 客户端 ID 严格递增；high-water mark 保证不复用且只占 O(1) 内存。
            OrderId last_id_ = 0;
            // 终态窗口：固定大小环形缓冲，避免每个终态订单的哈希/分配开销。
            struct TerminalEntry {
                OrderId id = 0;
                OrderStatus status = OrderStatus::Rejected;
            };
            static constexpr std::size_t kTerminalLimit = 4096;
            std::array<TerminalEntry, kTerminalLimit> terminal_{};
            std::size_t terminal_count_ = 0;
            std::size_t terminal_next_ = 0;
            Sequence next_seq_ = 0;   // 订单与成交共用单调序号
    };

}  // namespace exchange
