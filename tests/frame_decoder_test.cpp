#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "exchange/frame_decoder.hpp"

using namespace exchange;

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition))                                                      \
            throw std::runtime_error(std::string("failed: ") + #condition);    \
    } while (false)

namespace {

std::string make_frame(const std::string& payload) {
    std::string frame(4, '\0');
    const auto size = static_cast<std::uint32_t>(payload.size());
    frame[0] = static_cast<char>(size & 0xffu);
    frame[1] = static_cast<char>((size >> 8) & 0xffu);
    frame[2] = static_cast<char>((size >> 16) & 0xffu);
    frame[3] = static_cast<char>((size >> 24) & 0xffu);
    frame += payload;
    return frame;
}

void test_partial_frame() {
    FrameDecoder decoder;
    const auto frame = make_frame("hello");
    decoder.append(frame.data(), 2);
    REQUIRE(!decoder.next().has_value());
    REQUIRE(decoder.buffered() == 2);

    decoder.append(frame.data() + 2, frame.size() - 2);
    const auto payload = decoder.next();
    REQUIRE(payload.has_value() && *payload == "hello");
    REQUIRE(!decoder.next().has_value());
    REQUIRE(!decoder.failed());
}

void test_multiple_frames() {
    FrameDecoder decoder;
    const auto frames = make_frame("one") + make_frame("two");
    decoder.append(frames.data(), frames.size());
    REQUIRE(decoder.next() == std::optional<std::string>{"one"});
    REQUIRE(decoder.next() == std::optional<std::string>{"two"});
    REQUIRE(!decoder.next().has_value());
}

void test_oversized_frame_rejected() {
    FrameDecoder decoder(64);
    const std::string frame = make_frame(std::string(65, 'x'));
    decoder.append(frame.data(), frame.size());
    REQUIRE(!decoder.next().has_value());
    REQUIRE(decoder.failed());
}

void test_zero_length_rejected() {
    FrameDecoder decoder;
    const auto frame = make_frame("");
    decoder.append(frame.data(), frame.size());
    REQUIRE(!decoder.next().has_value());
    REQUIRE(decoder.failed());
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

    run("partial_frame", test_partial_frame);
    run("multiple_frames", test_multiple_frames);
    run("oversized_frame_rejected", test_oversized_frame_rejected);
    run("zero_length_rejected", test_zero_length_rejected);

    if (failed != 0) {
        std::cout << failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
