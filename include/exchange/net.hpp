#pragma once

#include <cstddef>
#include <cstdint>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace exchange {
namespace net {

// TCP 单帧 payload 上限：防止损坏流触发超大内存分配
inline constexpr std::uint32_t kMaxFrameSize = 1u << 20;  // 1 MiB

// TCP 帧格式：[uint32_t 小端长度][payload]
std::string encode_frame(std::string_view payload);

bool send_exact(int fd, const void* data, std::size_t size);
bool send_frame(int fd, std::string_view payload);
std::optional<std::string> recv_frame(int fd);

int make_listener(std::uint16_t port);
int connect_to(const char* host, std::uint16_t port);
bool set_nonblocking(int fd);
bool set_tcp_nodelay(int fd);

// ---- UDP / 组播 ----

struct UdpSocket {
    int fd = -1;
    sockaddr_in target{};
};

// 创建 UDP 发送端；multicast=true 时设置 TTL/loop 并按组播地址发送
std::optional<UdpSocket> udp_sender(const char* host, std::uint16_t port,
                                    bool multicast);

// 绑定 UDP 接收端；multicast_group 非空时加入组播组。返回非阻塞 socket。
int udp_receiver(std::uint16_t port, const char* multicast_group);

// 绑定并返回非阻塞控制 socket（用于快照请求/响应）
int udp_control_socket(std::uint16_t port);

std::optional<sockaddr_in> udp_address(const char* host, std::uint16_t port);
bool udp_send_to(int fd, const sockaddr_in& target, std::string_view payload);

// 非阻塞读取；无数据或错误返回 nullopt。sender 可空。
std::optional<std::pair<std::string, sockaddr_in>> udp_recv_from(
    int fd, sockaddr_in* sender = nullptr);

}  // namespace net
}  // namespace exchange
