# MiniExchange 项目说明（Claude 工作上下文）

## 项目是什么

MiniExchange 是一个 C++20 教学项目：模拟交易系统的订单簿与撮合引擎，并逐步加入 TCP 订单接入、SPSC 无锁队列与性能基准。目标是作为量化 C++ 开发方向的求职作品，同时让项目所有者（用户）通过分阶段实现学习对应技术栈。

**主线文档是 [TUTORIAL.md](TUTORIAL.md)**：包含环境搭建、5 个阶段的教程、每阶段的检查点自测与面试追问。**README.md 是项目门面**，随进度更新路线图勾选。

## 构建与测试命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# TSan 独立构建目录（勿与 ASan 混用）
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"

# 基准（阶段五）
cmake -S . -B build -DMINIEX_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
```

## 代码约定

- C++20，所有代码位于命名空间 `exchange`
- 公共头文件在 `include/exchange/`（目录与命名空间一致），实现在 `src/`
- 命名：类型 PascalCase，函数/变量 snake_case，成员变量尾部下划线
- 错误处理：热路径用错误码返回（不抛异常）；IO 边界可返回错误或 nullopt
- 价格一律 `int64_t` tick 整数（1 tick = 0.01），**禁止 double 进入订单簿**
- 时间优先用引擎分配的单调 seq，**禁止依赖系统时钟或哈希遍历顺序**（保证确定性回放）
- 撮合核心单线程模型；并发输入经 SPSC 队列串行化

## 教学约定（重要）

- 用户的目标是**学习**：优先引导用户自己实现并解释设计决策，只做用户明确要求代做的部分
- 参考实现已写在 TUTORIAL.md 各阶段中；用户卡住时再出示，不要提前代写整个阶段
- 性能数字只能来自本机实测并记录环境（CPU/编译器/编译选项/负载），**禁止照抄其他项目的数字**
- 每完成一个阶段：运行该阶段检查点测试 → 更新 README 路线图勾选 → git tag（v0.1…v1.0）并推送
- 提交信息：Conventional Commits 风格，中文描述（feat:/test:/bench:/docs:/refactor:）
