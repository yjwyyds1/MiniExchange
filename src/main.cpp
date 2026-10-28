// 命令行演示程序：手动输入订单命令，观察撮合结果与盘口变化。
// 命令格式：
//   N <id> <B|S> <price> <qty>   新单（价格单位 tick，1 tick = 0.01；市价单 price 填 0）
//   C <id>                       撤单
//   S <id>                       查询订单状态（生命周期）
//   Q                            打印顶层盘口快照
//   X                            退出
#include <iostream>
#include <limits>
#include <string>

#include "exchange/matching_engine.hpp"

using namespace exchange;

namespace {

const char* errorName(ErrorCode e) {
    switch (e) {
        case ErrorCode::Ok:                 return "OK";
        case ErrorCode::InvalidOrderId:     return "BAD_ID";
        case ErrorCode::DuplicateOrderId:   return "DUP_ID";
        case ErrorCode::UnknownOrderId:     return "UNKNOWN_ID";
        case ErrorCode::InvalidPrice:       return "BAD_PRICE";
        case ErrorCode::InvalidQuantity:    return "BAD_QTY";
    }
    return "?";
}

const char* statusName(OrderStatus s) {
    switch (s) {
        case OrderStatus::New:           return "New";
        case OrderStatus::PartialFilled: return "PartialFilled";
        case OrderStatus::Filled:        return "Filled";
        case OrderStatus::Cancelled:     return "Cancelled";
        case OrderStatus::Rejected:      return "Rejected";
    }
    return "?";
}

// 打印时顺手换算成"元"便于人工核对——注意：double 只出现在展示层，
// 订单簿内的存储与比较永远用整数 tick。
void printTrades(const std::vector<Trade>& trades) {
    for (const auto& t : trades) {
        std::cout << "  成交: 买#" << t.buy_id << " x 卖#" << t.sell_id
                  << "  价格 " << t.price << " tick (" << t.price / 100.0
                  << " 元)  数量 " << t.qty << "  seq " << t.seq << '\n';
    }
}

void printSnapshot(const Snapshot& s) {
    std::cout << "  盘口: bid " << s.best_bid << " x " << s.bid_qty
              << "  |  ask " << s.best_ask << " x " << s.ask_qty
              << "  (seq " << s.seq << ")\n";
}

void printUsage() {
    std::cout << "命令:\n"
              << "  N <id> <B|S> <price> <qty>   新单（tick 价格，市价单 price=0）\n"
              << "  C <id>                       撤单\n"
              << "  S <id>                       查询订单状态\n"
              << "  Q                            打印盘口快照\n"
              << "  X                            退出\n\n";
}

// 解析失败时丢弃本行剩余输入，避免死循环
void skipRestOfLine(std::istream& in) {
    in.clear();
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

}  // namespace

int main() {
    MatchingEngine engine;
    printUsage();

    char cmd;
    while (std::cin >> cmd) {
        switch (cmd) {
            case 'N': {
                Order order;
                char side = 0;
                std::uint64_t id = 0;
                std::int64_t price = 0;   // 有符号：负价格可以走通引擎的非法价格校验
                std::uint64_t qty = 0;
                if (!(std::cin >> id >> side >> price >> qty)) {
                    std::cout << "  格式错误，应为: N <id> <B|S> <price> <qty>\n";
                    skipRestOfLine(std::cin);
                    break;
                }
                if (side != 'B' && side != 'S') {
                    std::cout << "  方向只能是 B 或 S\n";
                    skipRestOfLine(std::cin);
                    break;
                }
                order.id = id;
                order.side = (side == 'B') ? Side::Buy : Side::Sell;
                order.type = (price == 0) ? OrderType::Market : OrderType::Limit;
                order.price = static_cast<Price>(price);
                order.qty = qty;

                const auto r = engine.submit(order);
                std::cout << (r.accepted ? "  已受理 #" : "  未受理 #") << id << '\n';
                if (r.error != ErrorCode::Ok) {
                    std::cout << "  提示: " << errorName(r.error) << '\n';
                }
                if (r.accepted)
                    std::cout << "  结果: " << statusName(r.status)
                              << "  未成交 " << r.unfilled_qty << '\n';
                printTrades(r.trades);
                break;
            }
            case 'C': {
                std::uint64_t id = 0;
                if (!(std::cin >> id)) {
                    skipRestOfLine(std::cin);
                    break;
                }
                std::cout << (engine.cancel(id) ? "  已撤销 #" : "  撤销失败 #")
                          << id << '\n';
                break;
            }
            case 'S': {
                std::uint64_t id = 0;
                if (!(std::cin >> id)) {
                    skipRestOfLine(std::cin);
                    break;
                }
                const auto st = engine.statusOf(id);
                if (st) {
                    std::cout << "  #" << id << " 状态: " << statusName(*st) << '\n';
                } else {
                    std::cout << "  #" << id << " 不存在或终态已归档\n";
                }
                break;
            }
            case 'Q':
                printSnapshot(engine.snapshot());
                break;
            case 'X':
                return 0;
            default:
                std::cout << "  未知命令 '" << cmd << "'\n";
                skipRestOfLine(std::cin);
                break;
        }
    }
    return 0;
}
