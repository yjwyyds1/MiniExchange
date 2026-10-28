#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace exchange {
namespace net {

// 单帧 payload 上限：防止损坏流触发超大内存分配
inline constexpr std::uint32_t kMaxFrameSize = 1u << 20;  // 1 MiB

// 帧格式：[uint32_t 小端长度][payload]
std::string encode_frame(std::string_view payload);

bool send_exact(int fd, const void* data, std::size_t size);
bool send_frame(int fd, std::string_view payload);
std::optional<std::string> recv_frame(int fd);

int make_listener(std::uint16_t port);
int connect_to(const char* host, std::uint16_t port);

}  // namespace net
}  // namespace exchange
