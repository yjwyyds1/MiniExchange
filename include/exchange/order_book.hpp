#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

#include "types.hpp"

namespace exchange {

    // 价格档位维护精确的 128 位累计量（高/低 64 位），展示时饱和。
    struct PriceLevel {
        std::list<std::shared_ptr<Order>> orders;
        Quantity low = 0;
        Quantity high = 0;
        void add(Quantity qty);
        void subtract(Quantity qty);
        Quantity depth() const;
    };

    // 订单簿：只负责"存放与查询"，不负责撮合规则
    class OrderBook {
        public:
            bool insert(const std::shared_ptr<Order>& order);  // 挂单
            void remove(const std::shared_ptr<Order>& order);  // 撤单/完全成交移除
            void reduce(const std::shared_ptr<Order>& order, Quantity filled_qty);

            std::shared_ptr<Order> best(Side side) const;      // 最优价上的队首订单
            std::optional<Price>   bestPrice(Side side) const;
            Quantity               depthAtBest(Side side) const; // 最优价档位总量
            bool                   empty(Side side) const;
            std::size_t            orderCount() const;         // 统计/诊断用

        private:
            // 两侧都用默认升序 map（同一类型）；
            // 方向差异在访问处体现：买单最优价取 rbegin()（最大键），
            // 卖单最优价取 begin()（最小键）。
            std::map<Price, PriceLevel> bids_;
            std::map<Price, PriceLevel> asks_;
            struct Location {
                Side side;
                Price price;
                std::list<std::shared_ptr<Order>>::iterator iterator;
            };
            std::unordered_map<OrderId, Location> locations_;
            std::size_t count_ = 0;
    };

}  // namespace exchange
