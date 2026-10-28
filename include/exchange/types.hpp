#pragma once

#include <cstdint>

namespace exchange {

    using Price    = std::int64_t;   // 价格：tick 的整数倍，1 tick = 0.01
    using Quantity = std::uint64_t;  // 数量：整数单位（股/张）
    using OrderId  = std::uint64_t;
    using Sequence = std::uint64_t;  // 到达序号：时间优先与回放对拍的依据

    enum class Side : std::uint8_t { Buy, Sell };

    enum class OrderType : std::uint8_t { Limit, Market };

    enum class OrderStatus : std::uint8_t {
        New,            // 已挂单，未成交
        PartialFilled,  // 部分成交，剩余挂单
        Filled,         // 完全成交
        Cancelled,      // 已撤销（含市价单未成交被撤销）
        Rejected        // 校验失败
    };

    // 订单：小对象、值语义，引擎内部按需拷贝
    struct Order {
        OrderId     id = 0;
        Side        side = Side::Buy;
        OrderType   type = OrderType::Limit;
        Price       price = 0;   // 市价单不使用（置 0）
        Quantity    qty = 0;     // 剩余数量
        Sequence    seq = 0;     // 由引擎在到达时分配，调用方不要填
        OrderStatus status = OrderStatus::New;
    };

    // 一笔成交
    struct Trade {
        OrderId  buy_id = 0;
        OrderId  sell_id = 0;
        Price    price = 0;      // 以簿内挂单价成交
        Quantity qty = 0;
        Sequence seq = 0;        // 成交序号（全局单调），回放对拍靠它
    };

    // 盘口快照（顶层一档），阶段三行情输出会用到
    struct Snapshot {
        Price    best_bid = 0;
        Quantity bid_qty = 0;
        Price    best_ask = 0;
        Quantity ask_qty = 0;
        Sequence seq = 0;
    };

    enum class ErrorCode : std::uint8_t {
        Ok,
        InvalidOrderId,     // ID 必须大于 0 且严格递增
        DuplicateOrderId,   // 重复 ID
        UnknownOrderId,     // 撤单时 ID 不存在
        InvalidPrice,       // 限价单价格非法
        InvalidQuantity     // 数量为 0
    };

}  // namespace exchange
