#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

#include "exchange/net.hpp"

using namespace exchange::net;

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition))                                                      \
            throw std::runtime_error(std::string("failed: ") + #condition);    \
    } while (false)

namespace {

void make_pair(int fds[2]) {
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
}

// 半包：先发 3 字节，延迟后发剩余部分，recv_frame 必须等齐一整帧
void test_half_frame_reassembled() {
    int fds[2];
    make_pair(fds);

    const std::string frame = encode_frame("hello");
    REQUIRE(send_exact(fds[0], frame.data(), 3));

    std::thread remaining([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        send_exact(fds[0], frame.data() + 3, frame.size() - 3);
    });

    const auto payload = recv_frame(fds[1]);
    remaining.join();
    REQUIRE(payload.has_value());
    REQUIRE(*payload == "hello");

    ::close(fds[0]);
    ::close(fds[1]);
}

// 粘包：一次写入两帧，两次 recv_frame 各得一帧
void test_coalesced_frames() {
    int fds[2];
    make_pair(fds);

    const std::string both = encode_frame("one") + encode_frame("two");
    REQUIRE(send_exact(fds[0], both.data(), both.size()));

    const auto first = recv_frame(fds[1]);
    const auto second = recv_frame(fds[1]);
    REQUIRE(first.has_value() && *first == "one");
    REQUIRE(second.has_value() && *second == "two");

    ::close(fds[0]);
    ::close(fds[1]);
}

// 对端关闭：recv_frame 返回空
void test_eof() {
    int fds[2];
    make_pair(fds);
    ::close(fds[0]);

    REQUIRE(!recv_frame(fds[1]).has_value());
    ::close(fds[1]);
}

// 非法长度：超过上限或为 0 的帧必须被拒绝，且不能分配超大内存
void test_invalid_length_rejected() {
    int fds[2];
    make_pair(fds);

    std::string header(4, '\0');
    const std::uint32_t too_large = kMaxFrameSize + 1;
    header[0] = static_cast<char>(too_large & 0xffu);
    header[1] = static_cast<char>((too_large >> 8) & 0xffu);
    header[2] = static_cast<char>((too_large >> 16) & 0xffu);
    header[3] = static_cast<char>((too_large >> 24) & 0xffu);
    REQUIRE(send_exact(fds[0], header.data(), header.size()));
    REQUIRE(!recv_frame(fds[1]).has_value());

    std::string zero(4, '\0');
    REQUIRE(send_exact(fds[0], zero.data(), zero.size()));
    REQUIRE(!recv_frame(fds[1]).has_value());

    ::close(fds[0]);
    ::close(fds[1]);
}

}  // namespace

int main() {
    int failed = 0;
    const auto run = [&](const char* name, void (*fn)()) {
        try {
            fn();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << name << ": " << e.what() << '\n';
        }
    };

    run("half_frame_reassembled", test_half_frame_reassembled);
    run("coalesced_frames", test_coalesced_frames);
    run("eof", test_eof);
    run("invalid_length_rejected", test_invalid_length_rejected);

    if (failed != 0) {
        std::cout << failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
