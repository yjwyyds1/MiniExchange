#pragma once

#include <unordered_map>
#include <deque>
#include <vector>

#include "order_book.hpp"
#include "types.hpp"

namespace exchange {

    // 撮合引擎：单线程模型。校验、撮合、订单生命周期管理都在这里。
    class MatchingEngine {
        public:
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

        private:
            std::vector<Trade> match(Order& taker);  // 与对手方逐档撮合
            void eraseIndex(OrderId id);             // 订单终结时从索引移除

            OrderBook book_;
            // 订单索引：id -> 订单。簿与索引各持一份 shared_ptr，生命周期安全
            std::unordered_map<OrderId, std::shared_ptr<Order>> orders_;
            // 客户端 ID 严格递增；high-water mark 保证不复用且只占 O(1) 内存。
            OrderId last_id_ = 0;
            static constexpr std::size_t kTerminalLimit = 4096;
            std::unordered_map<OrderId, OrderStatus> terminal_;
            std::deque<OrderId> terminal_order_;
            Sequence next_seq_ = 0;   // 订单与成交共用单调序号
    };

}  // namespace exchange
