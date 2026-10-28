#include "exchange/journal.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace exchange::journal {
namespace {
constexpr std::size_t kHeader = 20;
constexpr std::uint32_t kMaxPayload = 16u * 1024u * 1024u;

void sync_file(const std::filesystem::path& path) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open file for sync");
    const BOOL ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok) throw std::runtime_error("file sync failed");
#else
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open file for sync");
    const int ok = fsync(fd);
    close(fd);
    if (ok != 0) throw std::runtime_error("file sync failed");
#endif
}

void replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& target) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("atomic checkpoint replace failed");
#else
    std::filesystem::rename(source, target);
#endif
}

void put32(std::string& out, std::uint32_t n) {
    for (int i = 0; i != 4; ++i) out.push_back(static_cast<char>((n >> (8 * i)) & 255));
}
void put64(std::string& out, std::uint64_t n) {
    for (int i = 0; i != 8; ++i) out.push_back(static_cast<char>((n >> (8 * i)) & 255));
}
std::uint32_t get32(std::string_view in, std::size_t pos) {
    std::uint32_t n = 0;
    for (int i = 0; i != 4; ++i)
        n |= std::uint32_t(static_cast<unsigned char>(in[pos+i])) << (8*i);
    return n;
}
std::uint64_t get64(std::string_view in, std::size_t pos) {
    std::uint64_t n = 0;
    for (int i = 0; i != 8; ++i)
        n |= std::uint64_t(static_cast<unsigned char>(in[pos+i])) << (8*i);
    return n;
}
std::uint32_t checksum(std::string_view bytes) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : bytes) h = (h ^ c) * 16777619u;
    return h;
}
std::string frame(std::string_view magic, std::uint32_t type, std::string_view payload) {
    if (payload.size() > kMaxPayload) throw std::runtime_error("journal payload too large");
    std::string out(magic);
    put32(out, 1); // format version, explicitly little endian
    put32(out, type);
    put32(out, static_cast<std::uint32_t>(payload.size()));
    out.append(payload);
    put32(out, checksum(out));
    return out;
}
std::string load_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open journal file");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
bool valid_frame(std::string_view bytes, std::size_t pos, std::string_view magic) {
    if (bytes.size() - pos < kHeader || bytes.substr(pos,4) != magic ||
        get32(bytes,pos+4) != 1) return false;
    const auto length = get32(bytes,pos+12);
    if (length > kMaxPayload || length > bytes.size() - pos - kHeader) return false;
    const auto end = pos + kHeader + length;
    return get32(bytes,end-4) == checksum(bytes.substr(pos,end-pos-4));
}
bool valid_later(std::string_view bytes, std::size_t pos, std::string_view magic) {
    for (auto found = bytes.find(magic,pos); found != std::string_view::npos;
         found = bytes.find(magic,found+1)) {
        if (valid_frame(bytes,found,magic)) return true;
    }
    return false;
}
std::optional<Checkpoint> read_slot(const std::filesystem::path& path) {
    const auto bytes = load_bytes(path);
    if (bytes.size() < 24 || bytes.substr(0,4) != "QCHK" || get32(bytes,4) != 1)
        throw std::runtime_error("invalid checkpoint header");
    const auto length = get32(bytes,16);
    if (length > kMaxPayload || bytes.size() != 24ull + length ||
        get32(bytes,20+length) != checksum(std::string_view(bytes).substr(0,20+length)))
        throw std::runtime_error("corrupt checkpoint");
    return Checkpoint{get64(bytes,8), bytes.substr(20,length)};
}
}  // namespace

ScanResult scan(const std::filesystem::path& path,
                const std::function<void(const Record&)>& accept) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open journal file");
    const auto size = std::filesystem::file_size(path);
    ScanResult result;
    std::uint64_t pos = 0;
    auto later_record = [&](std::uint64_t from) {
        const std::string bytes = load_bytes(path); // corruption path only
        return valid_later(bytes, static_cast<std::size_t>(from), "QJNL");
    };
    while (pos < size) {
        if (size - pos < kHeader) {
            if (later_record(pos+1)) throw std::runtime_error("corrupt middle record");
            result.truncated_tail = true;
            break;
        }
        std::string bytes(16, '\0');
        in.read(bytes.data(), 16);
        if (!in) throw std::runtime_error("journal header read failed");
        if (bytes.compare(0,4,"QJNL") != 0 || get32(bytes,4) != 1)
            throw std::runtime_error("invalid journal header");
        const auto length = get32(bytes,12);
        if (length > kMaxPayload) throw std::runtime_error("invalid journal length");
        if (length > size - pos - kHeader) {
            if (later_record(pos+1)) throw std::runtime_error("corrupt middle length");
            result.truncated_tail = true;
            break;
        }
        bytes.resize(kHeader + length);
        in.read(bytes.data()+16, static_cast<std::streamsize>(length+4));
        if (!in) throw std::runtime_error("journal payload read failed");
        if (get32(bytes,16+length) !=
            checksum(std::string_view(bytes).substr(0,16+length)))
            throw std::runtime_error("journal checksum mismatch");
        accept(Record{get32(bytes,8), bytes.substr(16,length)});
        pos += kHeader + length;
    }
    result.valid_bytes = pos;
    return result;
}

ReadResult read(const std::filesystem::path& path) {
    ReadResult result;
    const ScanResult scanned = scan(path, [&](const Record& record) {
        result.records.push_back(record);
    });
    result.truncated_tail = scanned.truncated_tail;
    result.valid_bytes = scanned.valid_bytes;
    return result;
}

Journal::Journal(const std::filesystem::path& path) : path_(path) {
    if (std::filesystem::exists(path_)) {
        const auto scanned = scan(path_, [](const Record&) {});
        if (scanned.truncated_tail) std::filesystem::resize_file(path_,scanned.valid_bytes);
    } else {
        std::ofstream created(path_,std::ios::binary);
        if (!created) throw std::runtime_error("cannot create journal");
    }
    stream_.open(path_, std::ios::binary | std::ios::app);
    if (!stream_) throw std::runtime_error("cannot open journal for append");
}
void Journal::append(std::uint32_t type, std::string_view payload) {
    const auto bytes = frame("QJNL",type,payload);
    stream_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream_.flush();
    if (!stream_) throw std::runtime_error("journal append failed");
}
void Journal::sync() {
    stream_.flush();
    if (!stream_) throw std::runtime_error("journal flush failed");
    sync_file(path_);
}

void save_checkpoint(const std::filesystem::path& base, std::uint64_t seq,
                     std::string_view blob) {
    if (blob.size() > kMaxPayload) throw std::runtime_error("checkpoint too large");
    const auto first = base.string()+".0";
    const auto second = base.string()+".1";
    // Use the slot with the older valid sequence; a corrupt slot is replaceable.
    std::uint64_t a = 0, b = 0;
    try { if (std::filesystem::exists(first)) a = read_slot(first)->seq; } catch (...) {}
    try { if (std::filesystem::exists(second)) b = read_slot(second)->seq; } catch (...) {}
    const std::string target = (a <= b) ? first : second;
    std::string bytes("QCHK"); put32(bytes,1); put64(bytes,seq);
    put32(bytes,static_cast<std::uint32_t>(blob.size())); bytes.append(blob);
    put32(bytes,checksum(bytes));
    const auto tmp = target+".tmp";
    { std::ofstream out(tmp,std::ios::binary | std::ios::trunc);
      out.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); out.flush();
      if (!out) throw std::runtime_error("checkpoint write failed"); }
    sync_file(tmp);
    replace_file(tmp,target);
}
std::optional<Checkpoint> load_checkpoint(const std::filesystem::path& base,
                                          std::vector<std::string>* warnings) {
    const auto first = base.string()+".0";
    const auto second = base.string()+".1";
    const bool exists_a = std::filesystem::exists(first);
    const bool exists_b = std::filesystem::exists(second);
    if (!exists_a && !exists_b) return std::nullopt;
    std::optional<Checkpoint> a,b;
    try { if (exists_a) a = read_slot(first); }
    catch (const std::exception& error) {
        if (warnings) warnings->push_back(first + ": " + error.what());
    }
    try { if (exists_b) b = read_slot(second); }
    catch (const std::exception& error) {
        if (warnings) warnings->push_back(second + ": " + error.what());
    }
    if (!a && !b) throw std::runtime_error("all checkpoint slots corrupt");
    if (!a) return b;
    if (!b || a->seq >= b->seq) return a;
    return b;
}
}  // namespace exchange::journal
