#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include "exchange/order_generator.hpp"
#include "exchange/replay.hpp"

using namespace exchange;

namespace {

void usage() {
    std::cout << "用法:\n"
              << "  replay gen <seed> <count> <events-file>   生成事件流\n"
              << "  replay run <events-file> <trades-file>    回放并输出成交\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const std::string mode = argv[1];
    try {
        if (mode == "gen") {
            if (argc != 5) {
                usage();
                return 1;
            }
            const auto seed = static_cast<std::uint64_t>(std::stoull(argv[2]));
            const auto count = static_cast<std::size_t>(std::stoull(argv[3]));

            OrderGenerator generator(seed);
            const auto events = generate_events(generator, count);

            std::ofstream out(argv[4], std::ios::binary);
            if (!out) {
                std::cerr << "无法写入 " << argv[4] << '\n';
                return 1;
            }
            write_events(events, out);
            std::cout << "generated " << events.size() << " events -> " << argv[4]
                      << '\n';
            return 0;
        }

        if (mode == "run") {
            if (argc != 4) {
                usage();
                return 1;
            }
            std::ifstream in(argv[2], std::ios::binary);
            if (!in) {
                std::cerr << "无法读取 " << argv[2] << '\n';
                return 1;
            }
            const auto events = read_events(in);
            if (!events) {
                std::cerr << "事件文件格式错误: " << argv[2] << '\n';
                return 1;
            }

            MatchingEngine engine;
            std::ofstream out(argv[3], std::ios::binary);
            if (!out) {
                std::cerr << "无法写入 " << argv[3] << '\n';
                return 1;
            }
            replay(*events, engine, out);
            std::cout << "replayed " << events->size() << " events -> " << argv[3]
                      << '\n';
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << '\n';
        return 1;
    }

    usage();
    return 1;
}
