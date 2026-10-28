#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace exchange::journal {

struct Record { std::uint32_t type; std::string payload; };
struct ReadResult {
    std::vector<Record> records;
    bool truncated_tail = false;
    std::uint64_t valid_bytes = 0;
};
struct ScanResult {
    bool truncated_tail = false;
    std::uint64_t valid_bytes = 0;
};
struct Checkpoint { std::uint64_t seq; std::string blob; };

// Process one bounded record at a time; use this for large journals.
ScanResult scan(const std::filesystem::path& path,
                const std::function<void(const Record&)>& accept);
ReadResult read(const std::filesystem::path& path);
class Journal {
public:
    explicit Journal(const std::filesystem::path& path);
    void append(std::uint32_t type, std::string_view payload);
    // append flushes C++ buffers; sync also requests durable OS flush.
    void sync();
private:
    std::filesystem::path path_;
    std::ofstream stream_;
};
void save_checkpoint(const std::filesystem::path& base, std::uint64_t seq,
                     std::string_view blob);
std::optional<Checkpoint> load_checkpoint(
    const std::filesystem::path& base,
    std::vector<std::string>* warnings = nullptr);

}  // namespace exchange::journal
