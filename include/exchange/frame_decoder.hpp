#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace exchange {

// 增量帧解析器：用于 epoll 非阻塞读，不依赖 recv 阻塞语义。
// 帧格式：[uint32_t 小端长度][payload]
class FrameDecoder {
public:
    explicit FrameDecoder(std::size_t max_frame = 1u << 20)
        : max_frame_(max_frame) {}

    void append(const char* data, std::size_t size) {
        buffer_.append(data, size);
    }

    // 取出一帧；数据不完整返回 nullopt。长度非法时进入 failed 状态。
    std::optional<std::string> next() {
        if (failed_) return std::nullopt;
        const std::size_t available = buffer_.size() - offset_;
        if (available < 4) return std::nullopt;

        const auto* header =
            reinterpret_cast<const unsigned char*>(buffer_.data() + offset_);
        const std::uint32_t length =
            static_cast<std::uint32_t>(header[0]) |
            (static_cast<std::uint32_t>(header[1]) << 8) |
            (static_cast<std::uint32_t>(header[2]) << 16) |
            (static_cast<std::uint32_t>(header[3]) << 24);

        if (length == 0 || length > max_frame_) {
            failed_ = true;
            buffer_.clear();
            offset_ = 0;
            return std::nullopt;
        }
        if (available - 4 < length) return std::nullopt;

        std::string payload(buffer_.data() + offset_ + 4, length);
        offset_ += 4 + length;
        compact();
        return payload;
    }

    bool failed() const { return failed_; }
    std::size_t buffered() const { return buffer_.size() - offset_; }

private:
    void compact() {
        if (offset_ == buffer_.size()) {
            buffer_.clear();
            offset_ = 0;
        } else if (offset_ >= 4096 && offset_ * 2 >= buffer_.size()) {
            buffer_.erase(0, offset_);
            offset_ = 0;
        }
    }

    std::string buffer_;
    std::size_t offset_ = 0;
    std::size_t max_frame_;
    bool failed_ = false;
};

}  // namespace exchange
