# MiniExchange 实战教程：从零构建 C++20 低延迟订单簿与撮合系统

> 本教程面向"想投递量化 C++ / 交易系统开发岗位"的求职者，假设你已有基础 C++ 能力（类、RAII、STL 容器、简单 CMake）。
> 全流程预计 **4–6 周**（每天 2–4 小时），分 5 个阶段，每阶段结束都有**检查点自测**和**面试追问**。
> 原则：**先自己想、自己写，卡住了再看参考实现**。直接抄完代码 = 什么都没学到。

---

## 第 0 章 这个项目为什么适合投量化 C++（以及什么时候该换）

### 0.1 结论：适合，不用换

量化 C++ 开发岗位（Prop Trading / 做市商 / 交易所 / 券商电子交易部门）的日常核心就是：

1. **订单簿与撮合** —— 交易所/做市系统的"心脏"，面试必考；
2. **低延迟数据通路** —— TCP/UDP 接入、无锁队列、内存序；
3. **系统化性能测量** —— 吞吐、尾延迟、perf 定位热点。

这三个正好是 MiniExchange 的三大模块。这个项目是量化 C++ 领域公认的"第一项目"，比它更贴切的替代品非常少。

### 0.2 和你实习经历的互补关系

| 你的实习已有 | 本项目补充 | 避免重复 |
| --- | --- | --- |
| C++17、fmtlog 日志 | 撮合规则、订单生命周期 | 不重写日志系统 |
| 共享内存 | SPSC 无锁队列、内存序 | 不做 IPC 框架 |
| TCP 通信 | 消息分帧、低延迟参数调优 | 做"订单语义"而不是通用传输 |
| Linux 调试 (GDB) | perf 性能分析、TSan 并发验证 | — |

简历上的叙事：**"实习做的是通信与调试基础设施，本项目把业务核心（撮合）和性能工程（测量→优化→复测）补齐。"**

### 0.3 什么时候该换项目

- 如果你目标是**量化研究 / 策略岗**（Python 为主）→ 这个项目优先级低，换回测框架；
- 如果你想投**纯后端 / 游戏 / 嵌入式**→ 换对应领域项目；
- 除此之外（量化 C++、C++ 系统开发、交易基础设施），保持这个项目并把它做完。

### 0.4 学习地图：每个模块对应哪些技术栈

| 模块 | 你在这个模块学到的技术点 |
| --- | --- |
| 撮合引擎 | 数据结构选型与复杂度分析、整数价格表示、RAII、对象生命周期 |
| 测试与工程化 | GoogleTest、CMake 工程组织、边界条件设计 |
| 订单流与回放 | 随机数种子与可复现性、事件序列化、确定性验证 |
| TCP 接入 | socket API、字节流分帧、阻塞/非阻塞 IO、TCP_NODELAY |
| SPSC 队列 | std::atomic、内存序（release/acquire）、缓存行与伪共享、TSan |
| 性能分析 | Google Benchmark、延迟分位数、Linux perf、优化闭环 |

---

## 第 1 章 环境搭建（Windows 11 + WSL2）

### 1.1 为什么用 WSL2 而不是虚拟机，也不是纯 Windows

- **目标岗位跑在 Linux 上**。perf、TSan、valgrind、socket 编程的教材假设都是 Linux。
- WSL2 比 VirtualBox/VMware **更轻、文件互通更顺**（VS Code 直接连进去开发），性能损耗对学习项目无影响。
- 纯 Windows (MSVC) 能做，但你会失去 perf 等工具，且与面试场景脱节。

**最重要的一个坑（务必记住）**：WSL2 里访问 Windows 盘符（`/mnt/d/...`）走 9P 文件系统，**编译速度可能慢 10 倍以上**。所以代码要放在 WSL 自己的文件系统里，例如 `~/dev/MiniExchange`。你电脑上现有的 `D:\project\MiniExchange` 可以留作文档备份，开发主目录放 WSL 内。

### 1.2 安装步骤

**第 1 步：安装 WSL2 + Ubuntu 24.04**

在 Windows 上以**管理员身份**打开 PowerShell：

```powershell
wsl --install -d Ubuntu-24.04
```

- 重启电脑；首次启动 Ubuntu 会要求创建 Linux 用户名和密码（记住这个密码，sudo 要用）。
- 如果报错"未启用虚拟机平台"：重启进 BIOS 开启虚拟化（Intel VT-x / AMD-V），并在"控制面板 → 程序 → 启用或关闭 Windows 功能"中勾选**虚拟机平台**和**适用于 Linux 的 Windows 子系统**。

**第 2 步：安装工具链**

进入 Ubuntu 后执行：

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential gdb cmake git ninja-build valgrind
```

验证版本：

```bash
g++ --version      # 期望 12 及以上（Ubuntu 24.04 自带 13.x）
cmake --version    # 期望 3.22 及以上（自带 3.28）
gdb --version
```

**第 3 步：Git 与 GitHub**

```bash
git config --global user.name "你的名字"
git config --global user.email "你的邮箱"
ssh-keygen -t ed25519 -C "你的邮箱"   # 一路回车
cat ~/.ssh/id_ed25519.pub              # 复制内容
```

把公钥粘贴到 GitHub → Settings → SSH and GPG keys → New SSH key。测试：`ssh -T git@github.com`。

（可选）安装 GitHub CLI，之后创建仓库一条命令搞定：`sudo apt install gh`。

**第 4 步：VS Code（强烈推荐）**

在 **Windows** 上安装 [VS Code](https://code.visualstudio.com/)，装三个扩展：

- **WSL**（ms-vscode-remote.remote-wsl）
- **C/C++**（ms-vscode.cpptools）
- **CMake Tools**（ms-vscode.cmake-tools）

然后在 WSL 终端里，进入项目目录执行 `code .`——VS Code 会通过 WSL 扩展"进入"Linux 环境，编辑、终端、调试全部在 Linux 里进行。**这是 C++ 开发的标准姿势。**

### 1.3 用 CMake 构建第一个程序（15 分钟热身）

```bash
mkdir -p ~/dev/MiniExchange && cd ~/dev/MiniExchange
```

创建 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.22)
project(MiniExchange VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hello src/hello.cpp)
target_compile_options(hello PRIVATE -Wall -Wextra -Wpedantic)
```

创建 `src/hello.cpp`：

```cpp
#include <iostream>
#include <string_view>

int main() {
    std::string_view msg = "MiniExchange 环境就绪";
    std::cout << msg << '\n';
    return 0;
}
```

构建（**out-of-source build**，产物在 build/ 目录，源码目录保持干净）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/hello
```

**学习点**：搞懂三个概念——`-S`（源码目录）/`-B`（构建目录）分离的好处；`CMAKE_CXX_STANDARD` 与编译器 `-std=c++20` 的关系；target 式写法（`target_compile_options` 只作用于 hello 这个目标）vs 全局变量写法的区别。

### 1.4 Git 仓库与提交节奏

```bash
# 在 ~/dev/MiniExchange 下
git init -b main
# 创建 .gitignore：
#   build/
#   build-*/
#   compile_commands.json
git add -A && git commit -m "chore: 初始化项目骨架"
```

**养成习惯**（这也是面试谈资）：

- 每完成一个阶段的功能，跑通测试后提交，并打里程碑标签：
  `git tag v0.1 && git push origin v0.1`
- 提交信息用 Conventional Commits 风格：`feat:` `test:` `bench:` `docs:` `refactor:`，描述用中文。
- 一个小提交 = 一个可解释的意图。面试官会看你的提交历史。

### 1.5 perf 的 WSL2 注意事项（第 6 章会用到，先知道即可）

WSL2 的内核是微软定制版，`linux-tools-$(uname -r)` 通常装不上。应对办法：安装 `linux-tools-generic`（版本接近即可，基本可用）；如果 perf 实在跑不起来，退回 **valgrind callgrind**（第 6 章有完整替代方案）。这不是影响项目质量的致命问题，在性能报告里如实注明工具即可。

---

## 第 2 章 阶段一：撮合引擎 MVP（第 1 周）

### 2.1 本阶段目标与交付物

- ✅ `Order` / `OrderBook` / `MatchingEngine` 三类核心逻辑
- ✅ 限价单、市价单、撤单、部分成交、价格优先、时间优先、订单校验
- ✅ 一个命令行演示程序，能手工验证几笔交易
- ✅ 里程碑标签 `v0.1`

### 2.2 先想清楚再写代码：四个关键设计决策

**决策 1：价格用 `int64_t` 整数 tick 表示，绝不用 `double`**

- `0.1 + 0.2 != 0.3`：IEEE 754 浮点数无法精确表示十进制小数，价格相等比较会出错。
- 舍入行为在不同编译器/平台可能不一致 → 撮合结果不确定。
- NaN 会"污染"所有比较运算；`-0.0 == 0.0` 等边角坑防不胜防。
- 整数比较是单条指令，且完全确定。
- **真实世界做法**：交易所价格都用最小变动单位（tick）的整数倍表示（如 CME 的价格表示），`double` 只出现在定价模型里，不出现在订单簿里。

约定：`Price = int64_t`，1 tick = 0.01 元，价格 100.00 存成 `10'000`。

**决策 2：时间优先用"引擎分配的单调序号 seq"，不用时间戳**

- 时钟有分辨率、NTP 校准、回拨问题；重放时墙钟无法重建原始先后关系。
- seq 由引擎在订单**到达时**分配（`next_seq_++`），单调递增，撮合顺序完全由输入决定 → **确定性、可回放**（阶段三的核心前提）。
- 真实交易所对订单打序列号也是常见做法。

**决策 3：`std::map<Price, PriceLevel>` 存价格档位**

- 需要频繁取"最优价"：`map` 有序，best bid = `bids_.begin()`，best ask = `asks_.begin()`，都是 O(1)；
- `unordered_map` 无序，还得额外维护最值（堆或树），复杂度没有优势还多一份状态要同步；
- 插入/删除档位 O(log n)，对 MVP 足够。
- **升级路径（面试要会讲）**：真实系统价格域已知时用数组直接索引（O(1)），或用侵入式链表让撤单 O(1)。用 `map` 是正确的 MVP 抽象——`OrderBook` 类把实现隔离在接口后面，随时可换。

**决策 4：撤单先从"O(n) 扫描档内队列"做起，把 O(1) 升级留作思考题**

档内队列（`std::deque<shared_ptr<Order>>`）里找单删除是 O(档内数量)。升级方案：订单结构里缓存它在档内队列中的迭代器（`std::list` 的迭代器在增删其他元素时**不失效**），撤单变 O(1)。阶段一用简单版，阶段五再对比两者性能——这就是现成的优化实验素材。

### 2.3 接口设计（自己先写，写完对照）

目录结构（阶段一）：

```
MiniExchange/
├── CMakeLists.txt
├── include/exchange/
│   ├── types.hpp
│   ├── order_book.hpp
│   └── matching_engine.hpp
├── src/
│   ├── order_book.cpp
│   ├── matching_engine.cpp
│   └── main.cpp            # 命令行演示
└── tests/                  # 阶段二再建
```

**`include/exchange/types.hpp`**：

```cpp
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
    DuplicateOrderId,   // 重复 ID
    UnknownOrderId,     // 撤单时 ID 不存在
    InvalidPrice,       // 限价单价格非法
    InvalidQuantity,    // 数量为 0
    NotEnoughLiquidity  // 市价单无法全部成交（剩余部分按 IOC 撤销）
};

}  // namespace exchange
```

**`include/exchange/order_book.hpp`**：

```cpp
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "types.hpp"

namespace exchange {

// 单个价格档位：同价格订单按 seq 先后排列（deque 头部优先）
using PriceLevel = std::deque<std::shared_ptr<Order>>;

// 订单簿：只负责"存放与查询"，不负责撮合规则
class OrderBook {
public:
    bool insert(const std::shared_ptr<Order>& order);  // 挂单
    void remove(const std::shared_ptr<Order>& order);  // 撤单/完全成交移除

    std::shared_ptr<Order> best(Side side) const;      // 最优价上的队首订单
    std::optional<Price>   bestPrice(Side side) const;
    Quantity               depthAtBest(Side side) const; // 最优价档位总量
    bool                   empty(Side side) const;
    std::size_t            orderCount() const;         // 统计/诊断用

private:
    // 买单价高者优先（greater），卖单价低者优先（less）
    std::map<Price, PriceLevel, std::greater<>> bids_;
    std::map<Price, PriceLevel, std::less<>>    asks_;
    std::size_t count_ = 0;
};

}  // namespace exchange
```

**`include/exchange/matching_engine.hpp`**：

```cpp
#pragma once

#include <unordered_map>
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
        std::vector<Trade> trades;   // 本次提交产生的成交（可能多笔多价）
    };

    // 按值接收订单（小对象值语义）：引擎统一分配 seq，保证时间优先的权威性
    SubmitResult submit(Order order);

    // 撤销：true=成功；订单不存在或已终结返回 false
    bool cancel(OrderId id);

    std::optional<OrderStatus> statusOf(OrderId id) const;  // 生命周期查询
    Snapshot snapshot() const;                              // 顶层行情快照
    const OrderBook& book() const { return book_; }

private:
    std::vector<Trade> match(Order& taker);  // 与对手方逐档撮合
    void eraseIndex(OrderId id);             // 订单终结时从索引移除

    OrderBook book_;
    // 订单索引：id -> 订单。簿与索引各持一份 shared_ptr，生命周期安全
    std::unordered_map<OrderId, std::shared_ptr<Order>> orders_;
    Sequence next_seq_ = 0;   // 订单与成交共用单调序号
};

}  // namespace exchange
```

### 2.4 实现要点与参考代码

**Step 1：`order_book.cpp`**

```cpp
#include "exchange/order_book.hpp"

#include <algorithm>
#include <stdexcept>

namespace exchange {

bool OrderBook::insert(const std::shared_ptr<Order>& order) {
    auto& levels = (order->side == Side::Buy) ? bids_ : asks_;
    // try_emplace：档位不存在时创建，存在时不破坏已有内容
    levels.try_emplace(order->price).first->second.push_back(order);
    ++count_;
    return true;
}

void OrderBook::remove(const std::shared_ptr<Order>& order) {
    auto& levels = (order->side == Side::Buy) ? bids_ : asks_;
    auto it = levels.find(order->price);
    if (it == levels.end()) return;
    auto& queue = it->second;
    auto pos = std::find(queue.begin(), queue.end(), order);
    if (pos == queue.end()) return;
    queue.erase(pos);
    --count_;
    if (queue.empty()) levels.erase(it);  // 档位清空则删除该价格
}

std::shared_ptr<Order> OrderBook::best(Side side) const {
    const auto& levels = (side == Side::Buy) ? bids_ : asks_;
    if (levels.empty()) return nullptr;
    const auto& queue = levels.begin()->second;
    return queue.empty() ? nullptr : queue.front();
}

std::optional<Price> OrderBook::bestPrice(Side side) const {
    const auto& levels = (side == Side::Buy) ? bids_ : asks_;
    if (levels.empty()) return std::nullopt;
    return levels.begin()->first;
}

Quantity OrderBook::depthAtBest(Side side) const {
    const auto& levels = (side == Side::Buy) ? bids_ : asks_;
    if (levels.empty()) return 0;
    Quantity total = 0;
    for (const auto& o : levels.begin()->second) total += o->qty;
    return total;
}

bool OrderBook::empty(Side side) const {
    const auto& levels = (side == Side::Buy) ? bids_ : asks_;
    return levels.empty();
}

std::size_t OrderBook::orderCount() const { return count_; }

}  // namespace exchange
```

注意点：`OrderBook` 不感知校验和撮合规则，职责单一；`depthAtBest` 每次 O(档内数量)，如果以后高频调用，给档位维护一个"总量缓存"（插入/删除时增量更新）——又一个优化实验素材。

**Step 2：`matching_engine.cpp` 的撮合循环（核心中的核心，逐行看懂）**

```cpp
#include "exchange/matching_engine.hpp"

#include <algorithm>

namespace exchange {

std::vector<Trade> MatchingEngine::match(Order& taker) {
    std::vector<Trade> trades;

    while (taker.qty > 0) {
        // 1. 取对手方最优价上的最优先订单（价格优先 + 时间优先都由簿保证）
        const Side opp = (taker.side == Side::Buy) ? Side::Sell : Side::Buy;
        const auto resting = book_.best(opp);
        if (!resting) break;  // 对手方无单，无法成交

        // 2. 限价单检查价格是否触及（市价单跳过价格检查，直接成交）
        if (taker.type == OrderType::Limit) {
            const bool crossed = (taker.side == Side::Buy)
                ? (resting->price <= taker.price)      // 买价 >= 最低卖价
                : (resting->price >= taker.price);     // 卖价 <= 最高买价
            if (!crossed) break;                        // 未触及，剩余挂单
        }

        // 3. 按挂单价成交 min(双方剩余数量)
        const Quantity qty = std::min(taker.qty, resting->qty);
        const auto [buy_id, sell_id] = (taker.side == Side::Buy)
            ? std::pair{taker.id, resting->id}
            : std::pair{resting->id, taker.id};
        trades.push_back(Trade{buy_id, sell_id, resting->price, qty, next_seq_++});

        taker.qty -= qty;
        resting->qty -= qty;

        // 4. 挂单被吃光：移出订单簿，标记 Filled，移出索引
        if (resting->qty == 0) {
            book_.remove(resting);
            resting->status = OrderStatus::Filled;
            eraseIndex(resting->id);
        } else {
            resting->status = OrderStatus::PartialFilled;
        }
    }
    return trades;
}

MatchingEngine::SubmitResult MatchingEngine::submit(Order order) {
    SubmitResult result;

    // ---- 校验（在分配 seq 之前失败 = 拒绝，不留下任何状态）----
    if (order.qty == 0) { result.error = ErrorCode::InvalidQuantity; return result; }
    if (order.type == OrderType::Limit && order.price <= 0) {
        result.error = ErrorCode::InvalidPrice; return result;
    }
    if (orders_.contains(order.id)) {
        result.error = ErrorCode::DuplicateOrderId; return result;
    }

    // ---- 到达：分配序号，登记索引 ----
    order.seq = next_seq_++;
    auto sp = std::make_shared<Order>(order);
    orders_.emplace(order.id, sp);

    // ---- 撮合 ----
    result.trades = match(*sp);

    // ---- 收尾：按剩余数量决定去向 ----
    if (sp->qty > 0) {
        if (sp->type == OrderType::Market) {
            // 市价单按 IOC 语义：未成交部分直接撤销（真实交易所常见做法）
            sp->status = OrderStatus::Cancelled;
            eraseIndex(sp->id);
            result.error = ErrorCode::NotEnoughLiquidity;
            // accepted 仍为 false 表示"未全部成交"，但 trades 照常返回
        } else {
            sp->status = result.trades.empty() ? OrderStatus::New
                                               : OrderStatus::PartialFilled;
            book_.insert(sp);
        }
    } else {
        sp->status = OrderStatus::Filled;
        eraseIndex(sp->id);
    }
    result.accepted = true;
    return result;
}

bool MatchingEngine::cancel(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return false;  // 不存在或已终结
    auto& order = it->second;
    book_.remove(order);            // 若仍在簿内则移除
    order->status = OrderStatus::Cancelled;
    eraseIndex(id);
    return true;
}

std::optional<OrderStatus> MatchingEngine::statusOf(OrderId id) const {
    auto it = orders_.find(id);
    if (it == orders_.end()) return std::nullopt;
    return it->second->status;
}

Snapshot MatchingEngine::snapshot() const {
    Snapshot s;
    s.seq = next_seq_;
    s.best_bid = book_.bestPrice(Side::Buy).value_or(0);
    s.best_ask = book_.bestPrice(Side::Sell).value_or(0);
    s.bid_qty = book_.depthAtBest(Side::Buy);
    s.ask_qty = book_.depthAtBest(Side::Sell);
    return s;
}

void MatchingEngine::eraseIndex(OrderId id) { orders_.erase(id); }

}  // namespace exchange
```

**Step 3：命令行演示 `src/main.cpp`**——写一个从 stdin 读命令的小循环（或先写死几笔示例），命令格式：

```
N 100 B 10000 50     # 新限价单 id=100 买 100.00 数量 50
N 101 S 9990 30      # 新限价单 id=101 卖 99.90 数量 30
C 100                # 撤单 100
Q                    # 打印盘口快照
```

每笔操作打印返回的 trades，方便人工验证。

### 2.5 手工验证清单（对着做一遍）

1. 挂一个买 100.00×50，挂一个卖 101.00×30 → 无成交，各自挂单；
2. 挂一个卖 100.00×20 → 与买单成交 20@100.00，买单剩 30 保持挂单，状态 PartialFilled；
3. 挂一个市价卖 999 → 与剩余买单成交 30@100.00，剩余 969 被撤销（NotEnoughLiquidity）；
4. 撤掉一个不存在的订单 → cancel 返回 false；
5. 重复提交相同 id → DuplicateOrderId；
6. 价格 0 的限价单、数量 0 → 校验拒绝。

### 2.6 检查点自测（答不上来就回去读代码，别往下走）

1. 你的撤单操作时间复杂度是多少？怎么降到 O(1)？
2. 部分成交后，剩余数量按什么价格继续挂单？代码里哪一行体现？
3. 市价买单在空簿上提交，完整代码路径是什么？最终状态是什么？
4. 给出一个 `double` 做价格会出错的**具体例子**。
5. seq 在哪里分配？如果客户端自己传 seq 会有什么问题？
6. 为什么两个容器（簿 + id 索引）各持一个 shared_ptr 是安全的？什么时候会产生悬垂引用？
7. `std::map<Price, PriceLevel, std::greater<>>` 的 `begin()` 返回的是什么？为什么它就是 best bid？

### 2.7 面试追问（本阶段）

**Q：为什么价格用整数 tick 而不是 double？**
A：精度与确定性。0.1 在 IEEE 754 下无法精确表示，价格相等比较会出错；浮点舍入在不同平台可能不一致，破坏撮合确定性；NaN 会污染比较。整数比较是单条指令。真实交易所（如 CME）的价格表示就是 tick 整数。

**Q：时间优先为什么用序号而不是时间戳？**
A：序号单调、无时钟分辨率/回拨问题，且由引擎在到达时统一分配，撮合顺序完全由输入决定——这让整个引擎**确定性可回放**（阶段三的基石）。时间戳来自墙钟，重放时无法重建原始先后关系。

**Q：为什么用 std::map 而不是 unordered_map？**
A：撮合需要频繁取最优价。map 有序，best bid/ask 直接取 begin() 是 O(1)；unordered_map 无序，需要额外维护最值结构，多一份状态同步，复杂度没有优势。map 的 O(log n) 对 MVP 足够。真实系统升级路径：价格域已知时用数组索引 O(1)，或侵入式结构；OrderBook 接口隔离了实现，可以无痛替换。

**Q：市价单流动性不足怎么处理？**
A：我的实现按 IOC 语义：能成交多少成交多少，剩余撤销并报告。这是交易所常见做法。备选：整单拒绝（All-or-None）。要能说清楚你选的语义和理由。

**Q：两个线程同时 submit 会怎样？**
A：我的引擎是单线程模型（阶段一明确假设）。并发提交会数据竞争。处理方式在阶段四：用 SPSC 队列把"并发生产"串行化为"单线程消费"，撮合核心永远单线程——这是真实交易系统的主流设计（匹配引擎单线程化换取确定性和低延迟），比给引擎加锁更好。

**Q：订单生命周期怎么管理？为什么用 shared_ptr？**
A：一张订单同时存在于"id 索引"和"订单簿档位"两个容器中，shared_ptr 保证在撤单与撮合的交错路径上不会悬垂。代价是引用计数原子操作与分配。生产系统更常用 unique_ptr + 侵入式容器（订单生命周期完全由引擎控制），本项目把它作为阶段五优化实验。

---

## 第 3 章 阶段二：测试与工程化（第 2 周）

### 3.1 目标与交付物

- ✅ GoogleTest 接入，撮合规则全覆盖测试
- ✅ CMake 重构为多目标工程（核心库 / 演示程序 / 测试）
- ✅ README 初稿、`docs/architecture.md` 初稿
- ✅ 里程碑 `v0.2` —— **此时项目已可写进简历（v0.1 版本描述）**

### 3.2 CMake 工程组织

```cmake
cmake_minimum_required(VERSION 3.22)
project(MiniExchange VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(MINIEX_BUILD_TESTS "构建单元测试" ON)
option(MINIEX_BUILD_BENCH "构建性能基准" OFF)   # 阶段五打开

# 核心库：撮合引擎，不含 main
add_library(exchange_core
  src/order_book.cpp
  src/matching_engine.cpp
)
target_include_directories(exchange_core PUBLIC include)
target_compile_options(exchange_core PRIVATE -Wall -Wextra -Wpedantic)

# 演示程序
add_executable(cli_demo apps/cli_demo.cpp)
target_link_libraries(cli_demo PRIVATE exchange_core)

# ---- 测试（FetchContent 自动下载，无需手工安装）----
if(MINIEX_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(matching_engine_test tests/matching_engine_test.cpp)
  target_link_libraries(matching_engine_test PRIVATE exchange_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(matching_engine_test)
endif()
```

**学习点**：

- **核心逻辑做成静态库** `exchange_core`，演示程序和测试都链接它——这是"程序入口与业务逻辑分离"的工程化第一步；
- `target_include_directories(... PUBLIC ...)`：链接 exchange_core 的目标自动获得 include 路径；
- `FetchContent`：测试依赖自动下载固定版本（v1.14.0），任何人克隆仓库都能构建，**可复现**；
- 第一次 configure 需要网络；以后 `cmake --build build` 直接可用。

构建与运行：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### 3.3 必写测试清单

**原则：测试断言的是"成交序列"，而不只是最终状态。** 比如一笔大单扫过三档价格，断言应为"产生 3 笔成交，价格分别是 100.00、100.01、100.02"，这样任何一笔错了都能定位。

| 测试用例 | 覆盖规则 | 为什么重要 |
| --- | --- | --- |
| 价格优先 | 更高买价优先成交 | 撮合第一定律 |
| 时间优先 | 同价先到先成交 | 撮合第二定律 |
| 部分成交（买方） | 大买单吃小卖单，剩余挂单 | 订单生命周期核心 |
| 部分成交（卖方） | 对称场景 | 方向对称性易写错 |
| 限价不触及 | 价格未交叉则挂单 | 最常见场景 |
| 市价单扫多档 | 一单产生多笔成交、价格序列 | 撮合循环正确性 |
| 市价单流动性不足 | IOC 撤销 + 已成交部分保留 | 边界语义 |
| 撤单成功 | 撤后盘口正确、状态 Cancelled | 簿维护 |
| 撤单不存在 ID | 返回 false | 边界 |
| 撤单已完全成交订单 | 返回 false | 生命周期竞态 |
| 重复订单 ID | 拒绝 + 原订单不受影响 | 校验 |
| 非法价格 / 数量 | 拒绝 | 校验 |
| 空簿行为 | 无成交、快照为空 | 初始状态 |
| 档位清空 | 价格档全部成交后档位删除 | 簿结构维护 |
| 快照正确性 | best bid/ask 与档位总量 | 行情输出 |

### 3.4 测试写法参考（两个示例，其余自己写）

```cpp
#include <gtest/gtest.h>

#include "exchange/matching_engine.hpp"

using namespace exchange;

namespace {

// 辅助：构造一个订单
Order make(OrderId id, Side side, OrderType type, Price price, Quantity qty) {
    Order o;
    o.id = id; o.side = side; o.type = type; o.price = price; o.qty = qty;
    return o;
}

// 价格优先：卖 99.90 与卖 100.00 同时在簿，买单必须与 99.90 成交
TEST(MatchingEngineTest, PricePriorityBestAskFilledFirst) {
    MatchingEngine engine;
    engine.submit(make(1, Side::Sell, OrderType::Limit, 10'000, 10)); // 100.00
    engine.submit(make(2, Side::Sell, OrderType::Limit, 9'990, 10));  // 99.90（更优）

    const auto r = engine.submit(make(3, Side::Buy, OrderType::Limit, 9'990, 5));
    ASSERT_TRUE(r.accepted);
    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.trades[0].sell_id, 2u);        // 与更优的卖单成交
    EXPECT_EQ(r.trades[0].price, 9'990);
    EXPECT_EQ(r.trades[0].qty, 5u);
}

// 时间优先 + 部分成交：同价两笔卖单，先到的先成交
TEST(MatchingEngineTest, TimePriorityAndPartialFill) {
    MatchingEngine engine;
    engine.submit(make(1, Side::Sell, OrderType::Limit, 10'000, 10));
    engine.submit(make(2, Side::Sell, OrderType::Limit, 10'000, 10)); // 同价后到

    const auto r = engine.submit(make(3, Side::Buy, OrderType::Limit, 10'000, 15));
    ASSERT_EQ(r.trades.size(), 2u);
    EXPECT_EQ(r.trades[0].sell_id, 1u);        // 先到先成交 10
    EXPECT_EQ(r.trades[0].qty, 10u);
    EXPECT_EQ(r.trades[1].sell_id, 2u);        // 后到的成交 5，剩余 5 挂单
    EXPECT_EQ(r.trades[1].qty, 5u);
    EXPECT_EQ(engine.statusOf(2), OrderStatus::PartialFilled);
}

}  // namespace
```

### 3.5 检查点自测

1. 你的"时间优先"测试能否区分**同价不同时**与**不同价**两种情形？如果引擎把时间优先实现错了，你的测试会失败吗？
2. 为什么断言成交**序列**比断言最终簿状态更好？
3. `gtest_discover_tests` 做了什么？不用它行不行？
4. 哪些边界条件你还没覆盖？对照上表补齐。
5. 你的 README 里"如何构建、如何测试"的步骤，一个全新电脑上的陌生人能照着跑通吗？（克隆到临时目录试一次。）

### 3.6 面试追问（本阶段）

**Q：你怎么测试时间优先？**
A：构造同价两笔卖单（不同 id、不同到达顺序），一笔买单恰好只够部分成交，断言成交序列是"先到的先成交"，且后到的剩余数量正确。关键是用**成交序列**断言而非最终状态。

**Q：如何保证测试可重复？**
A：测试不依赖随机数、不依赖时间戳（引擎内没有时钟）、不依赖哈希表遍历顺序——撮合顺序完全由 seq 决定。同样的输入在任何机器上产生同样的成交序列。

---

## 第 4 章 阶段三：订单流、事件回放与 TCP（第 3 周）

### 4.1 目标与交付物

- ✅ Order Generator：随机但**可复现**的订单流
- ✅ 事件回放：同一批事件重放两次，输出必须逐字节一致
- ✅ TCP Client/Server：长度前缀分帧，接入撮合引擎
- ✅ 里程碑 `v0.3`

### 4.2 Order Generator（订单生成器）

要点：固定种子的 `std::mt19937_64` → 同一种子永远产生同一条订单流。**随机性与可复现性并存**，这是回放和基准测试的地基。

```cpp
// include/exchange/order_generator.hpp
#pragma once

#include <cstdint>
#include <random>

#include "types.hpp"

namespace exchange {

// 事件：回放与网络传输的统一输入单元
struct Event {
    enum class Kind : std::uint8_t { NewOrder, CancelOrder };
    Kind    kind;
    Order   order;      // kind == NewOrder 时有效
    OrderId cancel_id;  // kind == CancelOrder 时有效
};

// 可复现的订单流：同一 seed 生成完全相同的 Event 序列
class OrderGenerator {
public:
    explicit OrderGenerator(std::uint64_t seed);

    Event next();  // 生成一个事件（新单或撤单）

private:
    std::mt19937_64 rng_;
    OrderId next_id_ = 1;
};

}  // namespace exchange
```

实现要点（`src/order_generator.cpp`）：

- 价格围绕中轴波动：`kMid = 10'000`（100.00），偏移 ±100 tick；
- 买单价格略低于中轴、卖单略高于中轴，让一部分订单**留在簿上**（否则全是立即成交，测不到簿维护）；
- 约 85% 限价单、10% 市价单、5% 撤单（从已生成且可能仍在簿内的 id 中随机挑——为简单起见，撤单失败由引擎正常返回 false，不算错误）。

```cpp
Event OrderGenerator::next() {
    static constexpr Price kMid = 10'000;
    const auto r = rng_() % 100;
    if (r < 5) {                        // 5% 撤单
        Event e;
        e.kind = Event::Kind::CancelOrder;
        e.cancel_id = 1 + rng_() % (next_id_ - 1);  // 随机挑一个历史 id
        return e;
    }
    Event e;
    e.kind = Event::Kind::NewOrder;
    e.order.id = next_id_++;
    e.order.side = (rng_() % 2) ? Side::Buy : Side::Sell;
    e.order.type = (rng_() % 10 < 9) ? OrderType::Limit : OrderType::Market;
    const auto offset = std::int64_t(rng_() % 200) - 100;
    e.order.price = kMid + offset + (e.order.side == Side::Buy ? -20 : 20);
    e.order.qty = rng_() % 100 + 1;
    return e;
}
```

### 4.3 事件回放：确定性的意义

**为什么交易系统要可回放？**

- 撮合逻辑改版后，用同一批历史事件重放，输出必须一致——**回归测试的最强形式**；
- 生产事故复盘：任何时点用事件流可以精确重建簿状态；
- 面试时这句话很值钱："我的引擎不读墙钟、不依赖哈希遍历顺序，输入相同则输出逐字节相同。"

**实现**（`apps/replay.cpp`）：

1. `OrderGenerator gen(42)` 生成 10 万条 Event，序列化成文本存盘（`events.log`）；
2. 重放：从 `events.log` 读入，逐条喂给引擎，把所有 Trade（带 seq）写出到 `trades.log`；
3. 再跑一次，`diff` 两次输出或比较 `sha256sum`，必须一致；
4. 以后每次改撮合代码，重跑回放作为回归测试。行为若有**有意**变更，更新基线并在提交信息里说明原因。

### 4.4 TCP 通信：字节流与分帧

**核心概念（面试必考）**：TCP 是**字节流**，不是消息流。一次 `send()` 不等于对端一次 `recv()`——对方可能一次收到两个包（粘包），也可能只收到半个包（拆包），取决于 MTU、内核缓冲、调度。所以必须自己定义**消息边界**。

本项目的帧格式（长度前缀法）：

```
┌──────────────────────┬────────────────────────────────┐
│ uint32_t length（小端）│  payload：length 字节的文本命令    │
└──────────────────────┴────────────────────────────────┘
```

payload 命令（文本协议，方便 telnet 调试）：

```
N 100 B 10000 50      # 新单：id=100 买 100.00 数量 50（市价单价格填 0）
C 100                 # 撤单 100
```

响应帧 payload（可多行）：

```
R OK                  # 受理
R REJ DUP_ID          # 拒绝：原因码
T 100 101 10000 50    # 成交：买方id 卖方id 价格 数量
S 10000 30 10020 40   # 快照：best_bid bid_qty best_ask ask_qty
```

**学习点**：文本协议好调试（可以直接 `nc` 测试）；生产系统用二进制协议（FIX/FAST/SBE）省带宽，但会引入字段对齐、字节序问题——能讲清这个取舍即可。

### 4.5 关键实现：`recv_exact` 与帧读取

```cpp
// 读满 n 字节：处理三种情况——不完整读、信号中断、对端关闭
bool recv_exact(int fd, void* buf, std::size_t n) {
    auto* p = static_cast<char*>(buf);
    while (n > 0) {
        const ssize_t r = ::recv(fd, p, n, 0);
        if (r == 0) return false;          // EOF：对端关闭连接
        if (r < 0) {
            if (errno == EINTR) continue;  // 被信号打断，重试
            return false;                  // 真错误
        }
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

// 读一帧：[4 字节长度][payload]
std::optional<std::string> recv_frame(int fd) {
    std::uint32_t len = 0;
    if (!recv_exact(fd, &len, sizeof(len))) return std::nullopt;
    if (len == 0 || len > kMaxFrameSize) {   // 上限校验：防损坏流/恶意流量导致超大分配
        return std::nullopt;
    }
    std::string payload(len, '\0');
    if (!recv_exact(fd, payload.data(), len)) return std::nullopt;
    return payload;
}

// 对应的 send_exact / send_frame：send 同样可能只发送一部分
bool send_exact(int fd, const void* buf, std::size_t n) {
    const auto* p = static_cast<const char*>(buf);
    while (n > 0) {
        const ssize_t s = ::send(fd, p, n, MSG_NOSIGNAL);  // 对端已关闭时不要 SIGPIPE 杀进程
        if (s < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += s;
        n -= static_cast<std::size_t>(s);
    }
    return true;
}
```

### 4.6 服务器结构（`apps/exchange_server.cpp`，单线程版）

阶段三**故意做成单线程**：accept 一个连接 → 循环读帧 → 解析 → 调引擎 → 回写响应帧 → 断开后再服务下一个。这很简单，但它有个致命弱点：网络 IO 阻塞时撮合就停了。**这个弱点正是阶段四 SPSC 存在的理由**——先亲手感受单线程的局限，再引入并发，理解会深得多。

```cpp
// 监听 socket：SO_REUSEADDR 允许重启后立刻复用端口
int make_listener(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    // 可选：关闭 Nagle 算法，降低小消息延迟（面试点：TCP_NODELAY）
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 只监听回环，安全起见
    addr.sin_port = htons(port);
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(fd, 8);
    return fd;
}

void serve(int client_fd, MatchingEngine& engine) {
    for (;;) {
        const auto frame = recv_frame(client_fd);
        if (!frame) break;                          // 对端关闭或协议错误
        const auto replies = apply(engine, *frame); // 解析 + 执行，产出多行响应
        for (const auto& line : replies) {
            if (!send_frame(client_fd, line)) return;
        }
    }
    ::close(client_fd);
}
```

`apply()` 是一个薄适配层（"Order Gateway"的雏形）：把文本命令解析成 `Event`，调用 `engine.submit/cancel`，再把结果格式化成响应文本。**分层**：协议处理与撮合逻辑互不感知。

客户端（`apps/sample_client.cpp`）：connect → 从 `OrderGenerator` 拿事件 → 组装命令帧 → send → recv 响应帧并打印 trades。

### 4.7 网络层测试（`tests/framing_test.cpp`）

用 `socketpair(AF_UNIX, SOCK_STREAM, 0, fds)` 创建一对本地 socket，在测试里模拟：

1. **半包**：发送方写 10 字节后停一下，接收方 `recv_frame` 必须等齐一帧；
2. **粘包**：一次写两帧，接收方两次 `recv_frame` 各得一帧；
3. **对端断开**：close 一端，另一端 `recv_frame` 返回 nullopt；
4. **非法长度**：构造长度字段超过上限的帧，必须被拒绝而不是分配巨量内存。

### 4.8 检查点自测

1. 用你自己的话解释：为什么"一次 send 对应一次 recv"是错的？给出一个具体拆包场景。
2. 长度字段为什么要有上限校验？没有会怎样？
3. `recv_exact` 里 `r == 0` 意味着什么？和 `r < 0` 的区别？
4. TCP_NODELAY 关掉的是什么？什么场景该开、什么场景不该开？
5. 你的回放测试能不能检测出"撮合循环少生成一笔成交"这种 bug？为什么能/不能？
6. 为什么阶段三的服务器是单线程的？它的瓶颈在哪？阶段四如何解决？

### 4.9 面试追问（本阶段）

**Q：TCP 粘包/拆包怎么处理？**
A：TCP 是字节流，消息边界需要应用层定义。我用长度前缀分帧：4 字节小端长度 + payload。读端用 `recv_exact` 循环收满，处理不完整读（recv 返回部分数据）、EINTR（信号中断重试）、EOF（r==0，对端关闭）。另外对长度字段设上限，防止损坏流导致超大内存分配。

**Q：为什么响应也要分帧？**
A：双向都是字节流，客户端同样面临拆包。而且一单可能产生多笔成交 + 快照，多行响应需要一个边界。

**Q：TCP_NODELAY 是做什么的？**
A：关闭 Nagle 算法。Nagle 会攒小包等 ACK，减少网络开销但增加延迟。交易场景延迟优先，所以开启 TCP_NODELAY；代价是更多小包、带宽效率略降。这是延迟与吞吐的经典权衡，用场景说话。

**Q：回放为什么要求确定性？怎么保证的？**
A：引擎不读墙钟（时间优先用 seq）、不依赖哈希表遍历顺序、随机流种子固定、单线程顺序处理。改版后重放历史事件流，输出逐字节一致——这是撮合逻辑回归测试的最强形式，也是事故复盘的基础。

**Q：文本协议和二进制协议怎么选？**
A：文本协议可调试性极好（nc 直接测），适合教学与内部工具；生产行情/订单走二进制（FIX/FAST/SBE），带宽和解析开销小，代价是字段对齐、字节序、版本兼容问题。能讲清取舍比用什么更重要。

---

## 第 5 章 阶段四：SPSC 无锁队列（第 4 周）

### 5.1 目标与交付物

- ✅ 单生产者单消费者有界无锁队列，配 TSan 验证与压力测试
- ✅ 系统重构：网络/生成线程（Producer）→ SPSC → 撮合线程（Consumer）
- ✅ 里程碑 `v0.4` —— **此时项目可写进简历（v0.2 版本描述）**

### 5.2 为什么要 SPSC？与 mutex + queue 对比

| | `std::queue` + `std::mutex` | SPSC 无锁队列 |
| --- | --- | --- |
| 争用 | 每次 push/pop 都抢锁，多线程缓存行颠簸 | 读写只触及自己的索引，**无争用** |
| 系统调用 | 阻塞时 futex 陷入内核（微秒级） | 纯用户态原子操作（纳秒级） |
| 延迟分布 | 尾延迟被锁唤醒放大 | 稳定得多 |
| 适用场景 | 通用 | 单生产者单消费者——**交易系统数据通路恰好就是这种形态**（网络线程生产、撮合线程消费） |

不要凭空说"无锁就是快"——**阶段五要用自己的实测数据说话**（同样的负载，两个实现对比延迟分布，数字写进报告）。这个对比实验本身就是最有力的面试素材。

### 5.3 两个前置概念，必须彻底搞懂

**概念 1：缓存行与伪共享（False Sharing）**

CPU 缓存以 64 字节为最小单位（缓存行）在核心间同步。如果 `head_` 和 `tail_` 落在**同一个缓存行**，生产者写 head 会导致消费者所在核心的整个缓存行失效，消费者写 tail 又反杀生产者——两个核心互相无效化对方的数据，即使它们读写的是不同变量。解决方案：把两个原子变量放在不同缓存行上（`alignas(64)`，或加 padding）。

**概念 2：内存序——为什么"原子变量自动线程安全"是错的**

`std::atomic` 只保证**单个变量的读写是原子的**，不保证"先写数据再写 head"这个**顺序**对另一个线程可见。编译器优化和 CPU 乱序执行都可能重排指令。C++ 内存模型用 memory_order 表达约束：

- `relaxed`：只保证原子性，不保证任何顺序；
- `release`（写）：当前线程在此之前的**所有写**，对之后 acquire 读到该值的线程可见；
- `acquire`（读）：读到 release 写的值后，能看到对方 release 之前的所有写；
- release/acquire 成对构成 **synchronizes-with** 关系。

x86 CPU 天然有较强的内存模型（store-store 保序），很多错误"碰巧"跑不出来；但编译器优化照样会重排，ARM（苹果芯片、ARM 服务器）会真正乱序。**所以必须按 C++ 内存模型写正确，不能"x86 上跑对就行"。**

### 5.4 参考实现（先自己写一版，卡住再看）

```cpp
// include/exchange/spsc_queue.hpp
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

namespace exchange {

// 单生产者单消费者有界队列（Vyukov 风格）
// 约束：push 只允许一个生产者线程调用；pop 只允许一个消费者线程调用。
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity)
        : capacity_(roundUpPow2(capacity)), mask_(capacity_ - 1),
          buffer_(std::make_unique<Slot[]>(capacity_)) {}

    bool tryPush(const T& value) {
        // 读自己的索引不需要 acquire：只有生产者自己写 head_
        const auto head = head_.load(std::memory_order_relaxed);
        // acquire 读 tail_：与消费者的 release 写 tail_ 构成同步对。
        // 若观测到"槽位已空闲"，同时保证消费者对旧槽位的读全部完成，
        // 我们的新写入不会覆盖它正在读的数据。
        if (head - tail_.load(std::memory_order_acquire) == capacity_) {
            return false;  // 队列满（背压由调用方决定策略）
        }
        buffer_[head & mask_].value = value;
        // release 写 head_：发布数据。消费者 acquire 读到新 head 时，
        // 保证能看到我们对槽位的完整写入。
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(T& out) {
        // 读自己的索引不需要 acquire
        const auto tail = tail_.load(std::memory_order_relaxed);
        // acquire 读 head_：与生产者的 release 同步，保证槽位数据已完整写入
        if (head_.load(std::memory_order_acquire) == tail) {
            return false;  // 队列空
        }
        out = std::move(buffer_[tail & mask_].value);
        // release 写 tail_：归还槽位。生产者 acquire 读到时，
        // 保证我们已经读完了该槽位的数据。
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::size_t capacity() const { return capacity_; }
    std::size_t size() const {   // 仅观测用，弱一致即可
        return head_.load(std::memory_order_relaxed) -
               tail_.load(std::memory_order_relaxed);
    }

private:
    static std::size_t roundUpPow2(std::size_t n) {
        --n;
        n |= n >> 1;  n |= n >> 2;  n |= n >> 4;
        n |= n >> 8;  n |= n >> 16; n |= n >> 32;
        return n + 1;
    }

    static constexpr std::size_t kCacheLine = 64;

    struct Slot { T value; };

    // 两个索引分别独占缓存行，消除伪共享
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};  // 生产者写，消费者读
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};  // 消费者写，生产者读
    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<Slot[]> buffer_;
};

}  // namespace exchange
```

**设计要点，逐个能解释才算过关**：

1. **head/tail 用单调计数器而不是槽位下标**：`head - tail` 就是元素个数，满/空判断不会歧义（若只存下标，满和空都是 head==tail）。槽位下标 = 计数器 `& mask_`。
2. **容量取 2 的幂**：位与代替取模（`& mask_` vs `% capacity_`），取模是除法，慢。
3. **为什么读"自己的索引"用 relaxed**：自己的索引只有自己写，读到的必然是自己最后写进去的值，无需同步开销。
4. **为什么读"对方的索引"用 acquire**：见代码注释——它携带了对方 release 之前的所有写。
5. **队列满/空的策略**：`tryPush/tryPop` 返回 false，**背压策略交给调用方**——行情类数据可以丢，订单类数据不能丢。本项目订单场景用"自旋 + yield"重试（见 5.6）。能讲清"什么数据可以丢、什么不能丢"比"永远不丢"更专业。
6. **槽位伪共享**（进阶）：当 T 很小且 head/tail 相邻时，生产者写槽位 h 与消费者读槽位 t 可能落进同一缓存行。可以给 Slot 加 padding（`char pad_[64 - sizeof(T)]`）——先测量再决定要不要做（阶段五的又一个实验）。

### 5.5 正确性验证：三层防线

**第一层：与锁版参考实现对拍**（最有说服力）

写一个 `std::queue + std::mutex + condition_variable` 的参考实现，同一批随机操作流分别跑两个队列，断言行为一致（包括满/空返回值）。压力测试跑 10 分钟以上。

**第二层：ThreadSanitizer**

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan -j
./build-tsan/tests/spsc_queue_test
```

TSan 能抓住数据竞争，但**不能证明无锁算法正确**——它只报告它观察到的执行中的竞争。所以 TSan 通过 ≠ 队列正确，必须配压力对拍。这句话面试官问"你怎么验证无锁队列"时是满分答案的开头。

（若链接报找不到 libtsan：`sudo apt install libtsan2`。）

**第三层：不变量断言**

在测试里周期性断言：`0 <= head - tail <= capacity`；消费者取出的元素序列与生产者写入序列的**前缀**一致（对拍时记录双方日志比对）。

### 5.6 系统接入：把单线程服务器升级为双线程流水线

```
[生产者线程]                           [消费者线程]
OrderGenerator / TCP 读端 ──tryPush──▶ SPSC ──tryPop──▶ MatchingEngine
        │                                                   │
        └── 事件打时间戳（测端到端延迟）◀── 处理完算耗时 ────────┘
```

```cpp
// 生产者：持续生成事件压入队列（阶段四先用生成器模拟网络输入，
// 之后换成 TCP 读端，接口不变——这正是解耦的意义）
void producer_loop(SpscQueue<Event>& q, OrderGenerator& gen,
                   std::atomic<bool>& stop) {
    while (!stop.load(std::memory_order_relaxed)) {
        Event e = gen.next();
        e.enqueue_ns = now_ns();          // 打时间戳：端到端延迟测量的起点
        while (!q.tryPush(e) && !stop.load(std::memory_order_relaxed)) {
            std::this_thread::yield();    // 背压：满则让出 CPU 重试
        }
    }
}

// 消费者：撮合线程（永远单线程，保证确定性与无锁）
void consumer_loop(SpscQueue<Event>& q, MatchingEngine& engine,
                   std::atomic<bool>& stop) {
    Event e;
    while (!stop.load(std::memory_order_relaxed)) {
        if (q.tryPop(e)) {
            const auto lat = now_ns() - e.enqueue_ns;  // 端到端延迟样本
            apply(e, engine);                          // 复用阶段三的 apply
            record_latency(lat);
        } else {
            std::this_thread::yield();
        }
    }
}
```

**思考题（面试高频）**：这里队列空时用 `yield()` 自旋。什么时候该用 `condition_variable`（省 CPU，但唤醒有微秒级延迟），什么时候该纯自旋（纳秒级，但烧 CPU）？交易系统的答案是**混合**：先短暂自旋（几十微秒）等数据，等不到再睡到条件变量上——真实系统常用 event count / futex。你至少要把这个权衡讲清楚。

### 5.7 检查点自测

1. 把你的队列里所有 acquire 改成 relaxed，在 x86 上跑压力测试——它"碰巧"还能过吗？为什么这样不能证明正确？
2. `head - tail == capacity_` 为什么能正确判断"满"？计数器回绕时会不会出错？
3. 如果两个生产者线程同时 push，会发生什么？为什么 SPSC 不能解决，要换什么？
4. 伪共享在你的代码里出现了几处？各自是怎么消除的？
5. TSan 通过能证明队列正确吗？不能的话 TSan 的价值在哪？
6. 你的背压策略是什么？为什么行情场景和订单场景的策略应该不同？

### 5.8 面试追问（本阶段——面试官最爱深挖的地方）

**Q：SPSC 为什么比 mutex + queue 快？**
A：无争用（读写分离到各自索引）、纯用户态（无 futex 系统调用）、缓存行隔离（无伪共享）。**并且我实测过**：同样负载下延迟分布对比见我的 benchmark 报告（阶段五数据）。

**Q：release/acquire 在你的代码里分别保护了什么？**
A：生产者的 release 写 head，保证消费者 acquire 读到新 head 时能看到完整的槽位写入；消费者 release 写 tail，保证生产者 acquire 读到新 tail 时，消费者已读完旧槽位。两组同步对互为镜像。若全用 relaxed，编译器/CPU 可能重排"写槽位"与"写 head"，消费者可能读到未完成的数据——这是数据竞争（UB）。

**Q：为什么是 SPSC 不是 MPMC？**
A：交易数据通路天然是单生产者（网络线程）单消费者（撮合线程）。SPSC 可以在每个端点只维护一个索引，MPMC 需要更复杂的算法（每个生产者独立 head、CAS 竞争等），成本和延迟都高。我的系统按 SPSC 形态设计；若真要多生产者，聚合层再串行化，或换 MPMC 实现。

**Q：队列满了怎么办？**
A：取决于数据的性质。行情可以丢（最新优先），订单不能丢。我的实现返回 false 由调用方决定：订单场景自旋重试（可选加短暂 yield）；生产系统会加流控——让生产者慢下来（TCP 背压天然有这种机制）。

**Q：怎么验证你的无锁队列正确？**
A：三层：与锁版参考实现对拍（随机操作流 + 长时间压力）、TSan 抓竞争（但 TSan 通过不等于正确，它只报告观察到的竞争）、不变量断言（计数差在 [0, capacity] 内、取出序列是写入序列的前缀）。另外单测覆盖满/空/回绕边界。

---

## 第 6 章 阶段五：性能测量与分析（第 5–6 周）

### 6.1 目标与交付物

- ✅ Google Benchmark：不同负载下的吞吐量
- ✅ 端到端延迟分位数（P50 / P95 / P99）
- ✅ perf 热点分析与至少一次"测量 → 优化 → 复测"闭环
- ✅ `docs/benchmark.md` 性能报告 + 里程碑 `v1.0` —— **项目完整版写进简历**

### 6.2 测什么：三个维度

**维度 1：吞吐量**（订单/秒）——单线程引擎 + 三种负载：

| 负载 | 构成 | 考察点 |
| --- | --- | --- |
| 限价为主 | 95% 限价、5% 市价 | 簿插入、map 维护 |
| 撤单密集 | 50% 撤单 | 删除路径 |
| 成交密集 | 大量市价单扫单 | 撮合循环 |

**维度 2：延迟分位数**——只看平均值会掩盖尾延迟。一单扫穿三档产生的最大单笔耗时，往往发生在簿结构变更（档位创建/删除）时。报告 P50/P95/P99。

**维度 3：对比实验**（每个实验都要有假设 → 数据 → 结论）：

1. `-O2` vs `-O3 -march=native -flto`；
2. map 档位 vs 数组索引档位（价格域已知）；
3. 撤单 O(n) 扫描 vs 缓存 list 迭代器 O(1)；
4. 对象池 vs 每次 `make_shared`（订单分配是热路径大头）；
5. mutex 队列 vs SPSC 队列（延迟分布对比）。

### 6.3 Google Benchmark 接入

CMake 里打开 `MINIEX_BUILD_BENCH`，追加：

```cmake
if(MINIEX_BUILD_BENCH)
  FetchContent_Declare(
    benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
  )
  set(BENCHMARK_ENABLE_TESTING OFF)   # 不构建 benchmark 自带的测试
  FetchContent_MakeAvailable(benchmark)

  add_executable(engine_bench benchmark/engine_bench.cpp)
  target_link_libraries(engine_bench PRIVATE exchange_core benchmark::benchmark_main)
endif()
```

**`benchmark/engine_bench.cpp`**（注意测量卫生：预热、稳态、负载形态稳定）：

```cpp
#include <benchmark/benchmark.h>

#include <vector>

#include "exchange/matching_engine.hpp"
#include "exchange/order_generator.hpp"

using namespace exchange;

static void BM_SubmitOrder(benchmark::State& state) {
    MatchingEngine engine;
    OrderGenerator gen(42);

    // 预生成订单流，基准本身不测生成开销
    std::vector<Order> orders;
    orders.reserve(100'000);
    for (std::size_t i = 0; i < 100'000; ++i) {
        const Event e = gen.next();
        if (e.kind == Event::Kind::NewOrder) orders.push_back(e.order);
    }

    std::size_t i = 0;
    for (auto _ : state) {
        // 周期性重置引擎，避免簿无限膨胀导致性能漂移（保持负载形态稳定）
        if (i % 10'000 == 0) engine = MatchingEngine{};
        engine.submit(orders[i % orders.size()]);
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SubmitOrder)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
```

运行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMINIEX_BUILD_BENCH=ON
cmake --build build -j
./build/engine_bench
```

**测量卫生清单**（面试官会查）：固定 Release + 编译选项并记录；跑基准前机器不干别的（关浏览器、插电）；每个配置多跑几轮取中位数；报告里写全 CPU 型号（`lscpu`）、编译器版本、内核、编译选项。

### 6.4 延迟测量与分位数

端到端延迟在阶段四已经埋好了（事件时间戳）。收集 100 万样本后：

```cpp
std::sort(samples.begin(), samples.end());
double percentile(const std::vector<std::uint64_t>& s, double p) {
    return static_cast<double>(s[static_cast<std::size_t>(p / 100.0 * (s.size() - 1))]);
}
```

注意：单笔延迟测量本身有开销（`steady_clock::now()` 约 20–80ns），对 MVP 可接受，但要在报告里说明测量方法。进阶：批量时间戳、rdtsc、或 Google Benchmark 的 thread 级统计。

### 6.5 perf 入门（WSL2 见 1.5 的说明）

```bash
sudo apt install -y linux-tools-generic     # WSL2 下版本可能不完全匹配，一般可用

# 宏观统计：IPC、缓存缺失、分支预测
perf stat -e cycles,instructions,cache-misses,branch-misses ./build/engine_bench

# 采样找热点
perf record -g ./build/engine_bench
perf report    # 交互式：按函数/指令展开看占比
```

**备选方案**（perf 不可用时）：

```bash
valgrind --tool=callgrind ./build/engine_bench
callgrind_annotate callgrind.out.*    # 函数级耗时与调用次数
```

典型发现与优化（你要写进报告的，是**你自己项目的真实数据**，下面只是预期形态）：

- 热点在 `std::map` 查找/插入（内存不连续、指针追逐）→ 实验：换数组索引（价格域 ±1000 tick 内）→ 记录前后数据；
- 热点在 `make_shared` 分配 → 实验：对象池/预分配 → 记录前后数据；
- 热点在撤单 O(n) 扫描 → 实验：缓存迭代器 O(1) 撤单 → 记录前后数据。

### 6.6 性能报告模板（`docs/benchmark.md`）

```markdown
# 性能基准报告

## 测试环境
- CPU / 内存 / OS / 内核版本 / 编译器版本 / 编译选项（完整列出）
- 测量方法：Google Benchmark 版本、迭代次数、取中位数方式

## 吞吐量
| 负载 | 吞吐（订单/秒） | 备注 |
...

## 端到端延迟（SPSC 通路）
| 配置 | P50 | P95 | P99 | 样本数 |
...

## 对比实验
| 实验 | 假设 | 优化前 | 优化后 | 结论 |
...

## 已知局限
（单线程撮合、tick 粒度、测试机与其他进程干扰等——诚实列出）
```

### 6.7 检查点自测

1. 为什么吞吐高不代表延迟好？给一个具体场景。
2. 你的基准里，如果不周期性重置引擎，测量结果会怎样漂移？为什么？
3. P99 为什么比平均值重要？
4. perf 报告里排名前三的热点是什么？你针对哪个做了什么优化，数据变化多少？
5. 你的性能报告里，环境信息列全了吗？别人能复现吗？

### 6.8 面试追问（本阶段）

**Q：为什么吞吐和延迟都要测？**
A：吞吐是批量处理的速率，延迟是单笔的耗时。批量下吞吐高可能掩盖单笔尖刺（如偶发的大扫单触发档位重建）。交易所既关心处理容量也关心最坏延迟，所以两个都要，且延迟要看分位数。

**Q：为什么看 P99 不看平均值？**
A：尾延迟决定风险。平均值会被大量正常样本稀释，P99 暴露偶发的长耗时（缓存缺失、锁、簿结构变更）。交易系统对尾延迟极度敏感。

**Q：如何保证基准可信？**
A：固定编译选项与优化级别并记录；预热与稳态（避免簿无限膨胀）；多轮取中位数；测量时排除其他负载；报告完整环境。基准数字永远附环境与方法论，不报"最好的一次"。

**Q：perf 发现了什么，你做了什么？**
A：（回答你项目的真实数据）例如热点在 map 指针追逐 → 换数组索引档位后吞吐从 X 到 Y；或订单分配 → 对象池后 P99 从 X 到 Y。**每个优化都要有前后数据**，这是本项目的灵魂。

---

## 第 7 章 进阶扩展（做完基础再碰，控制范围）

按优先级排序：

1. **UDP 组播行情模拟 + 序号缺口检测 + 本地行情簿重建**——最贴近你实习方向、最加分的扩展。模拟行情源按组播发 `Trade/Snapshot` 事件（带序号），接收端检测序号缺口、请求快照重建本地簿。技术点：UDP socket、组播、丢包处理、序号逻辑。
2. **多合约订单簿**——`unordered_map<Symbol, MatchingEngine>`，练习接口抽象。
3. **订单类型扩展**：IOC / FOK / Stop 单——每种都带语义测试。
4. **自成交防护**（Self-Trade Prevention）：同一账户买卖单直接互撞的取消逻辑。
5. **FIX/SBE 二进制协议**：把文本协议换成真实世界的协议，讲字节序与字段编码。

**不要做**：真实交易所连接、完整风控、持久化数据库——范围爆炸，且学不到核心。

---

## 第 8 章 简历写法与面试准备

### 8.1 三个里程碑的简历条目

**v0.2 完成（撮合引擎 + 测试）后：**

> **MiniExchange｜C++20 订单簿与撮合引擎**
> 基于 C++20 实现简化订单簿与撮合引擎：价格以整数 tick 表示，支持限价单、市价单（IOC 语义）、撤单与部分成交；以引擎分配的单调序号实现价格优先、时间优先，保证撮合确定性；通过 CMake + GoogleTest 建立自动化测试，覆盖优先级规则、订单生命周期与异常输入；提供事件回放对拍，验证不同版本撮合逻辑输出一致。

**v0.4 完成（TCP + SPSC）后，追加：**

> 实现 TCP 订单接入：自定义长度前缀消息分帧，处理不完整读写、信号中断与连接断开，客户端/服务端集成撮合引擎；实现单生产者单消费者无锁环形队列（release/acquire 内存序、缓存行隔离消除伪共享），构建"网络生产 → 队列解耦 → 撮合消费"双线程流水线，通过 ThreadSanitizer 与锁版参考实现对拍验证并发正确性。

**v1.0 完成（性能分析）后，追加：**

> 使用 Google Benchmark 建立限价/撤单/成交密集三类负载的吞吐基准，测量端到端延迟 P50/P95/P99；结合 Linux perf 定位 CPU 热点，完成"测量 → 优化 → 复测"闭环（如订单对象池、撤单 O(1) 化），前后数据记录于性能报告；对比 SPSC 与 mutex 队列的延迟分布。

**写法纪律**：只写**已实现**的；每个技术名词背后都有代码和数据支撑；面试被追问任何一行都能展开。

### 8.2 高频面试题库（参考答案在前几章的"面试追问"里）

**撮合与订单簿**：整数价格的理由 / 时间优先的实现 / 容器选型与复杂度 / 撤单复杂度 / 市价单流动性不足 / 撮合确定性 / 订单生命周期管理 / 并发提交怎么办

**网络**：粘包拆包与分帧 / recv 返回值语义 / TCP_NODELAY / 帧长度上限 / 文本 vs 二进制协议

**并发**：SPSC 快在哪（带自己的数据）/ release-acquire 各保护什么 / 全 relaxed 会怎样 / 伪共享 / 满队列策略 / SPSC vs MPMC / 无锁队列的验证方法 / TSan 的局限 / 自旋 vs 条件变量

**性能**：吞吐 vs 延迟 / P99 的意义 / 基准可信度 / perf 热点与优化闭环 / 优化前后数据

**通用**：项目里最难的 bug 是什么、怎么定位的（准备好 1–2 个真实故事：例如某次回放对拍不一致，追到某个状态漏更新）；如果重新做，哪里会设计得不同（准备 1–2 个反思：如"档位总量应该做增量缓存"）。

---

## 第 9 章 常见坑清单

1. **在 `/mnt/d` 下编译**：WSL2 跨文件系统 IO 慢一个量级，代码放 `~/dev/`。
2. **CRLF 换行符**：Windows 编辑器改 Linux 文件 → `git config --global core.autocrlf input`。
3. **基准没开优化**：`-O0` 的基准数字没有任何意义；报告必须注明构建类型。
4. **TSan 与 ASan 同时开**：两个 sanitizer 互斥，分开构建目录测。
5. **`std::map` 迭代中 erase**：必须 `it = m.erase(it)`（C++11 起返回下一个迭代器），否则悬垂。
6. **`greater<>` 的 map 里 best bid 方向搞反**：begin() 是**最大**键，买入簿用 greater 才对。
7. **从簿里删除订单后忘了清 id 索引**：悬垂 shared_ptr 导致幽灵订单——你的状态流转表要画出来（New → Partial → Filled/Cancelled，每步两个容器的动作）。
8. **SIGPIPE**：向已关闭的 socket 写数据默认会**杀死进程**——`send` 加 `MSG_NOSIGNAL`。
9. **回放对拍时引擎里混入墙钟/随机数**：任何非确定源都会让 diff 失败——审计你的代码路径。
10. **spinning 死循环烧 CPU**：自旋循环里一定要检查 stop 标志并 yield。
11. **FetchContent 首次配置需要网络**：内网/代理环境注意；一旦配置成功，源码已缓存，离线可构建。
12. **把别家项目的性能数字写进自己简历**：环境不同数字没有意义，自己测的才经得起追问。

---

## 附录 A 参考资源

| 资源 | 学什么 |
| --- | --- |
| [cpp-matching-engine](https://github.com/ShaneWoloszyn/cpp-matching-engine) | 订单优先级、撤单、部分成交的另一种组织方式（对照设计，不照抄） |
| [market-data-pipeline](https://github.com/ashutoshdayal13/market-data-pipeline) | C++20 下 SPSC Ring Buffer、生产者消费者线程、TSan 的用法 |
| [GoogleTest 官方文档](https://google.github.io/googletest/) | 断言、fixture、参数化测试 |
| [Google Benchmark 文档](https://github.com/google/benchmark/blob/main/docs/user_guide.md) | 基准写法与自定义统计 |
| [cppreference: std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order) | 内存序的权威定义 |
| Preshing《[An Introduction to Lock-Free Programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)》 | 无锁编程入门必读 |
| Beej《[Guide to Network Programming](https://beej.us/guide/bgnet/)》 | socket 编程最友好的教材 |
| [1024cores: SPSC 队列](https://www.1024cores.net/home/lock-free-algorithms/queues) | Vyukov 的无锁队列系列 |

## 附录 B 时间安排（每天 2–4 小时）

| 时间 | 内容 | 完成标志 |
| --- | --- | --- |
| W1 D1–D2 | 环境、git、CMake 热身 | hello 构建成功，仓库有首个提交 |
| W1 D3–D5 | types / OrderBook / MatchingEngine | 手工验证清单全通过 |
| W1 D6–D7 | 演示程序 + 自查 + tag v0.1 | v0.1 推上 GitHub |
| W2 D1–D2 | CMake 重构 + GoogleTest 接入 | ctest 跑通 |
| W2 D3–D6 | 测试清单全部落地 | 覆盖率：清单 15 项全绿 |
| W2 D7 | README + architecture.md + tag v0.2 | **简历可写 v0.1 描述** |
| W3 D1–D3 | Order Generator + 回放对拍 | 两次回放 diff 为空 |
| W3 D4–D7 | TCP 分帧 + 客户端/服务端 + 测试 | 网络半包/粘包测试全绿，tag v0.3 |
| W4 D1–D3 | SPSC 实现 + 内存序笔记 | 单测 + 对拍通过 |
| W4 D4–D5 | 双线程流水线 + TSan | TSan 干净，tag v0.4（**简历可写 v0.2 描述**） |
| W4 D6–D7 | 延迟埋点 + 数据采集 | 有第一版延迟分位数 |
| W5 | Benchmark 三类负载 + 对比实验 | benchmark.md 第一版 |
| W6 | perf 分析 + 优化闭环 + 报告定稿 | tag v1.0，**简历完整版** |

---

> 最后一句：这个项目的价值不在"做完了"，而在你能解释的每一个决策、每一组数据。**遇到卡点，先想 30 分钟，再看参考实现，然后用自己的话把方案写进笔记。** 祝顺利。
