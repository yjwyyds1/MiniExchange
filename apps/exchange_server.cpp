#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <unistd.h>

#include "exchange/gateway.hpp"
#include "exchange/matching_engine.hpp"
#include "exchange/net.hpp"

using namespace exchange;

int main(int argc, char** argv) {
    std::uint16_t port = 9001;
    try {
        if (argc > 1) port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    } catch (const std::exception& e) {
        std::cerr << "端口参数错误: " << e.what() << '\n';
        return 1;
    }

    const int listener = net::make_listener(port);
    if (listener < 0) {
        std::cerr << "监听失败: " << std::strerror(errno) << '\n';
        return 1;
    }
    std::cout << "listening on 127.0.0.1:" << port << '\n' << std::flush;

    MatchingEngine engine;
    for (;;) {
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::cerr << "accept 失败: " << std::strerror(errno) << '\n';
            break;
        }
        std::cout << "client connected\n" << std::flush;

        for (;;) {
            const auto frame = net::recv_frame(client);
            if (!frame) break;  // 对端关闭或协议错误
            const auto response = apply_gateway_command(engine, *frame);
            if (!net::send_frame(client, response)) break;
        }

        ::close(client);
        std::cout << "client disconnected\n" << std::flush;
    }

    ::close(listener);
    return 0;
}
