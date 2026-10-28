#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "exchange/frame_decoder.hpp"
#include "exchange/gateway.hpp"
#include "exchange/matching_engine.hpp"
#include "exchange/net.hpp"
#include "exchange/spsc.hpp"

using namespace exchange;

namespace {

constexpr std::size_t kRequestCapacity = 1u << 14;
constexpr std::size_t kResponseCapacity = 1u << 14;
constexpr std::size_t kMaxOutputBuffer = 1u << 20;  // 1 MiB，慢客户端上限
constexpr std::size_t kMaxPendingRequests = 4096;

#ifdef EPOLLRDHUP
constexpr std::uint32_t kReadHangup = EPOLLRDHUP;
#else
constexpr std::uint32_t kReadHangup = 0;
#endif

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

struct Request {
    std::uint64_t conn_id = 0;
    std::string payload;
};

struct Response {
    std::uint64_t conn_id = 0;
    std::string payload;
};

struct Connection {
    int fd = -1;
    std::uint64_t id = 0;
    FrameDecoder decoder;
    std::string output;
    std::deque<std::string> pending;
    bool read_paused = false;
    bool want_close = false;
};

class Gateway {
public:
    Gateway(std::uint16_t port, bool et) : et_(et) { ok_ = setup(port); }

    ~Gateway() {
        g_stop.store(true, std::memory_order_relaxed);
        if (matcher_.joinable()) matcher_.join();
        for (auto& [fd, conn] : connections_) {
            (void)conn;
            ::close(fd);
        }
        if (event_fd_ >= 0) ::close(event_fd_);
        if (epoll_fd_ >= 0) ::close(epoll_fd_);
        if (listener_ >= 0) ::close(listener_);
    }

    bool ok() const { return ok_; }

    int run() {
        if (!ok_) return 1;
        std::vector<epoll_event> events(64);

        while (!g_stop.load(std::memory_order_relaxed)) {
            const int count = ::epoll_wait(epoll_fd_, events.data(),
                                           static_cast<int>(events.size()), 100);
            if (count < 0) {
                if (errno == EINTR) continue;
                std::cerr << "epoll_wait 失败: " << std::strerror(errno) << '\n';
                break;
            }
            for (int i = 0; i < count; ++i) {
                const int fd = events[i].data.fd;
                const std::uint32_t mask = events[i].events;
                if (fd == listener_) {
                    handle_accept();
                } else if (fd == event_fd_) {
                    drain_eventfd();
                    drain_responses();
                } else {
                    handle_connection(fd, mask);
                }
            }
            // 处理 eventfd 合并/背压恢复，并刷新所有待发送数据
            drain_responses();
        }

        std::cout << "gateway stopped: clients=" << accepted_
                  << " requests=" << requests_processed_ << '\n';
        return 0;
    }

private:
    bool setup(std::uint16_t port) {
        listener_ = net::make_listener(port);
        if (listener_ < 0 || !net::set_nonblocking(listener_)) {
            std::cerr << "监听失败: " << std::strerror(errno) << '\n';
            return false;
        }
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
        event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (epoll_fd_ < 0 || event_fd_ < 0) {
            std::cerr << "epoll/eventfd 创建失败\n";
            return false;
        }
        if (!add_fd(listener_, EPOLLIN) || !add_fd(event_fd_, EPOLLIN)) {
            std::cerr << "epoll_ctl 注册失败\n";
            return false;
        }
        matcher_ = std::thread([this] { matching_loop(); });
        std::cout << "epoll gateway listening on 127.0.0.1:" << port
                  << (et_ ? " (ET)" : " (LT)") << '\n';
        return true;
    }

    bool add_fd(int fd, std::uint32_t events) {
        epoll_event event{};
        event.events = events | (et_ ? EPOLLET : 0);
        event.data.fd = fd;
        return ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == 0;
    }

    void matching_loop() {
        while (!g_stop.load(std::memory_order_relaxed)) {
            Request request;
            if (!requests_.try_pop(request)) {
                std::this_thread::yield();
                continue;
            }
            ++requests_processed_;
            Response response{request.conn_id,
                              apply_gateway_command(engine_, request.payload)};

            while (!g_stop.load(std::memory_order_relaxed)) {
                if (responses_.try_push(response)) break;
                std::this_thread::yield();
            }
            if (g_stop.load(std::memory_order_relaxed)) break;

            const std::uint64_t one = 1;
            while (::write(event_fd_, &one, sizeof(one)) < 0 && errno == EINTR) {
            }
        }
    }

    void drain_eventfd() {
        std::uint64_t value = 0;
        while (::read(event_fd_, &value, sizeof(value)) > 0) {
        }
    }

    void handle_accept() {
        for (;;) {
            const int fd = ::accept(listener_, nullptr, nullptr);
            if (fd < 0) {
                if (errno == EINTR) continue;
                break;  // EAGAIN 或错误
            }
            net::set_nonblocking(fd);
            net::set_tcp_nodelay(fd);

            Connection connection;
            connection.fd = fd;
            connection.id = next_conn_id_++;
            connections_.emplace(fd, std::move(connection));
            id_to_fd_.emplace(next_conn_id_ - 1, fd);
            ++accepted_;

            if (!add_fd(fd, connection_mask(connections_.at(fd)))) {
                close_connection(fd);
                continue;
            }
            std::cout << "client connected fd=" << fd
                      << " id=" << (next_conn_id_ - 1) << '\n';
        }
    }

    std::uint32_t connection_mask(const Connection& connection) const {
        std::uint32_t mask = kReadHangup;
        if (!connection.read_paused) mask |= EPOLLIN;
        if (!connection.output.empty()) mask |= EPOLLOUT;
        return mask;
    }

    void update_events(Connection& connection) {
        if (connection.want_close) return;
        epoll_event event{};
        event.events = connection_mask(connection) | (et_ ? EPOLLET : 0);
        event.data.fd = connection.fd;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection.fd, &event);
    }

    void handle_connection(int fd, std::uint32_t mask) {
        const auto it = connections_.find(fd);
        if (it == connections_.end()) return;
        Connection& connection = it->second;

        if ((mask & (EPOLLERR | EPOLLHUP)) != 0) {
            connection.want_close = true;
        }
        if ((mask & (EPOLLIN | kReadHangup)) != 0 && !connection.want_close) {
            read_available(connection);
        }
        if ((mask & EPOLLOUT) != 0 && !connection.want_close) {
            flush_output(connection);
        }
        if (connection.want_close) {
            close_connection(fd);
        }
    }

    void read_available(Connection& connection) {
        char buffer[65536];
        for (;;) {
            const ssize_t n = ::recv(connection.fd, buffer, sizeof(buffer), 0);
            if (n > 0) {
                connection.decoder.append(buffer, static_cast<std::size_t>(n));
                if (!process_frames(connection)) return;
            } else if (n == 0) {
                connection.want_close = true;
                return;
            } else {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                connection.want_close = true;
                return;
            }
        }
    }

    bool process_frames(Connection& connection) {
        for (;;) {
            auto payload = connection.decoder.next();
            if (!payload) {
                if (connection.decoder.failed()) {
                    connection.want_close = true;
                    return false;
                }
                return true;
            }
            if (requests_.try_push(Request{connection.id, *payload})) {
                continue;
            }
            if (connection.pending.size() >= kMaxPendingRequests) {
                connection.want_close = true;  // 该客户端请求过载
                return false;
            }
            connection.pending.push_back(std::move(*payload));
            connection.read_paused = true;
            update_events(connection);
            return true;
        }
    }

    void drain_pending(Connection& connection) {
        while (!connection.pending.empty()) {
            if (!requests_.try_push(
                    Request{connection.id, connection.pending.front()})) {
                return;
            }
            connection.pending.pop_front();
        }
        if (connection.read_paused) {
            connection.read_paused = false;
            if (!process_frames(connection)) {
                connection.want_close = true;
                return;
            }
            update_events(connection);
        }
    }

    void drain_responses() {
        Response response;
        while (responses_.try_pop(response)) {
            const auto id_it = id_to_fd_.find(response.conn_id);
            if (id_it == id_to_fd_.end()) continue;
            const auto conn_it = connections_.find(id_it->second);
            if (conn_it == connections_.end()) continue;
            Connection& connection = conn_it->second;
            connection.output += net::encode_frame(response.payload);
            if (connection.output.size() > kMaxOutputBuffer) {
                connection.want_close = true;  // 慢客户端
            }
        }

        for (auto it = connections_.begin(); it != connections_.end();) {
            Connection& connection = it->second;
            if (!connection.want_close) flush_output(connection);
            if (!connection.want_close) drain_pending(connection);
            if (connection.want_close) {
                it = close_connection(it->first);
            } else {
                ++it;
            }
        }
    }

    void flush_output(Connection& connection) {
        while (!connection.output.empty()) {
            const ssize_t n = ::send(connection.fd, connection.output.data(),
                                     connection.output.size(), MSG_NOSIGNAL);
            if (n > 0) {
                connection.output.erase(0, static_cast<std::size_t>(n));
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            } else {
                connection.want_close = true;
                return;
            }
        }
        update_events(connection);
    }

    using ConnectionMap = std::unordered_map<int, Connection>;

    ConnectionMap::iterator close_connection(int fd) {
        const auto it = connections_.find(fd);
        if (it == connections_.end()) return connections_.end();
        std::cout << "client disconnected fd=" << fd << " id=" << it->second.id
                  << '\n';
        id_to_fd_.erase(it->second.id);
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        return connections_.erase(it);
    }

    int listener_ = -1;
    int epoll_fd_ = -1;
    int event_fd_ = -1;
    bool et_ = false;
    bool ok_ = false;

    ConnectionMap connections_;
    std::unordered_map<std::uint64_t, int> id_to_fd_;
    std::uint64_t next_conn_id_ = 1;
    std::uint64_t accepted_ = 0;
    std::uint64_t requests_processed_ = 0;

    SpscQueue<Request, kRequestCapacity> requests_;
    SpscQueue<Response, kResponseCapacity> responses_;
    MatchingEngine engine_;  // 只被 matcher 线程访问
    std::thread matcher_;
};

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 9001;
    bool et = false;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--et") {
                et = true;
            } else if (arg == "--port" && i + 1 < argc) {
                port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
            } else {
                std::cerr << "未知参数: " << arg << '\n';
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "参数错误: " << e.what() << '\n';
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    Gateway gateway(port, et);
    if (!gateway.ok()) return 1;
    return gateway.run();
}
