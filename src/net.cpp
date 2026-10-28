#include "exchange/net.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace exchange {
namespace net {

namespace {

void store_le32(std::uint32_t value, char* out) {
    out[0] = static_cast<char>(value & 0xffu);
    out[1] = static_cast<char>((value >> 8) & 0xffu);
    out[2] = static_cast<char>((value >> 16) & 0xffu);
    out[3] = static_cast<char>((value >> 24) & 0xffu);
}

std::uint32_t load_le32(const char* in) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(in[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[3])) << 24);
}

// 读满 size 字节：处理不完整读、EINTR、对端关闭
bool recv_exact(int fd, void* data, std::size_t size) {
    auto* p = static_cast<char*>(data);
    while (size > 0) {
        const ssize_t n = ::recv(fd, p, size, 0);
        if (n == 0) return false;  // EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

std::string encode_frame(std::string_view payload) {
    std::string frame(4, '\0');
    store_le32(static_cast<std::uint32_t>(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}

bool send_exact(int fd, const void* data, std::size_t size) {
    const auto* p = static_cast<const char*>(data);
    while (size > 0) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send(fd, p, size, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(fd, p, size, 0);
#endif
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

bool send_frame(int fd, std::string_view payload) {
    if (payload.size() > kMaxFrameSize) return false;
    const auto frame = encode_frame(payload);
    return send_exact(fd, frame.data(), frame.size());
}

std::optional<std::string> recv_frame(int fd) {
    char header[4];
    if (!recv_exact(fd, header, sizeof(header))) return std::nullopt;

    const std::uint32_t length = load_le32(header);
    if (length == 0 || length > kMaxFrameSize) return std::nullopt;

    std::string payload(length, '\0');
    if (!recv_exact(fd, payload.data(), payload.size())) return std::nullopt;
    return payload;
}

int make_listener(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 8) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

int connect_to(const char* host, std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace net
}  // namespace exchange
