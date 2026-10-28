#include "exchange/journal.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace journal = exchange::journal;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void require_throws(Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class TestFiles {
public:
    TestFiles() {
        static std::uint64_t next_id = 0;
        directory_ = fs::temp_directory_path() /
                     ("quantcore_journal_test_" + std::to_string(++next_id));
        if (fs::exists(directory_)) {
            throw std::runtime_error("test directory already exists");
        }
        fs::create_directory(directory_);
    }

    ~TestFiles() {
        for (const auto& name : {"events.bin", "checkpoint.0", "checkpoint.1",
                                 "checkpoint.0.tmp", "checkpoint.1.tmp"}) {
            std::error_code error;
            fs::remove(directory_ / name, error);
        }
        std::error_code error;
        fs::remove(directory_, error);
    }

    fs::path events() const { return directory_ / "events.bin"; }
    fs::path checkpoint() const { return directory_ / "checkpoint"; }

private:
    fs::path directory_;
};

void change_first_occurrence(const fs::path& path, std::string_view needle) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "open file to corrupt");
    std::string bytes(std::istreambuf_iterator<char>(input), {});
    const auto position = bytes.find(needle);
    require(position != std::string::npos,
            "expected payload in file: " + path.string() + " needle=" +
                std::string(needle));
    bytes[position] ^= 0x01;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(output.good(), "write corrupted file");
}

void normal_records_round_trip() {
    TestFiles files;
    const std::string binary_payload("A\0B", 3);
    journal::Journal events(files.events());
    events.append(7, binary_payload);
    events.append(0x12345678U, "second");
    events.sync();

    const auto result = journal::read(files.events());
    require(result.records.size() == 2, "read both records");
    require(result.records[0].type == 7 && result.records[0].payload == binary_payload,
            "preserve binary payload");
    require(result.records[1].type == 0x12345678U && result.records[1].payload == "second",
            "preserve record type and order");
    require(!result.truncated_tail, "complete file has no truncated tail");
    require(result.valid_bytes == fs::file_size(files.events()),
            "valid byte count reaches end of complete file");
    std::size_t streamed = 0;
    const auto scan = journal::scan(files.events(), [&](const journal::Record&) {
        ++streamed;
    });
    require(streamed == 2 && !scan.truncated_tail, "stream records without collecting them");

    std::ifstream input(files.events(), std::ios::binary);
    char header[8] = {};
    input.read(header, sizeof(header));
    require(std::string_view(header, 4) == "QJNL", "journal has magic");
    require(static_cast<unsigned char>(header[4]) == 1 && header[5] == 0 &&
                header[6] == 0 && header[7] == 0,
            "journal version uses explicit little endian encoding");
}

void truncated_tail_is_repaired_before_append() {
    TestFiles files;
    std::uint64_t first_end = 0;
    {
        journal::Journal events(files.events());
        events.append(1, "first-record");
        first_end = fs::file_size(files.events());
        events.append(2, "unfinished-record");
    }
    fs::resize_file(files.events(), fs::file_size(files.events()) - 3);

    const auto partial = journal::read(files.events());
    require(partial.truncated_tail, "detect incomplete final record");
    require(partial.records.size() == 1 && partial.records[0].payload == "first-record",
            "keep complete prefix");
    require(partial.valid_bytes == first_end, "report exact complete prefix length");

    journal::Journal reopened(files.events());
    reopened.append(3, "after-recovery");
    const auto recovered = journal::read(files.events());
    require(!recovered.truncated_tail && recovered.records.size() == 2,
            "reopen discards incomplete tail before appending");
    require(recovered.records[1].type == 3 && recovered.records[1].payload == "after-recovery",
            "new record follows preserved prefix");
}

void damaged_complete_record_stops_recovery() {
    TestFiles files;
    journal::Journal events(files.events());
    events.append(1, "first-record");
    events.append(2, "second-record");
    change_first_occurrence(files.events(), "first-record");
    require_throws([&] { (void)journal::read(files.events()); },
                   "middle record checksum failure must throw");
    require_throws([&] { journal::Journal reopened(files.events()); },
                   "writer must not append to damaged middle");
}

void newest_checkpoint_and_bad_slot_fallback() {
    TestFiles files;
    require(!journal::load_checkpoint(files.checkpoint()).has_value(),
            "no checkpoint before first save");
    journal::save_checkpoint(files.checkpoint(), 10, std::string("old\0state", 9));
    auto loaded = journal::load_checkpoint(files.checkpoint());
    require(loaded && loaded->seq == 10 && loaded->blob == std::string("old\0state", 9),
            "load first binary checkpoint");

    journal::save_checkpoint(files.checkpoint(), 20, "new-state");
    loaded = journal::load_checkpoint(files.checkpoint());
    require(loaded && loaded->seq == 20 && loaded->blob == "new-state",
            "load newest of two slots");

    change_first_occurrence(files.checkpoint().string() + ".1", "new-state");
    std::vector<std::string> warnings;
    loaded = journal::load_checkpoint(files.checkpoint(), &warnings);
    require(loaded && loaded->seq == 10 && loaded->blob == std::string("old\0state", 9),
            "fall back when newest slot fails checksum");
    require(!warnings.empty(), "report damaged checkpoint slot");

    journal::save_checkpoint(files.checkpoint(), 30, "recovered-state");
    loaded = journal::load_checkpoint(files.checkpoint());
    require(loaded && loaded->seq == 30 && loaded->blob == "recovered-state",
            "replace damaged slot on later save");
}

void two_damaged_checkpoint_slots_fail_closed() {
    TestFiles files;
    journal::save_checkpoint(files.checkpoint(), 1, "one");
    journal::save_checkpoint(files.checkpoint(), 2, "two");
    change_first_occurrence(files.checkpoint().string() + ".0", "one");
    change_first_occurrence(files.checkpoint().string() + ".1", "two");
    require_throws([&] { (void)journal::load_checkpoint(files.checkpoint()); },
                   "two bad slots cannot be mistaken for no checkpoint");
}

}  // namespace

int main() {
    try {
        normal_records_round_trip();
        truncated_tail_is_repaired_before_append();
        damaged_complete_record_stops_recovery();
        newest_checkpoint_and_bad_slot_fallback();
        two_damaged_checkpoint_slots_fail_closed();
        std::cout << "journal_test: all 5 cases passed\n";
    } catch (const std::exception& error) {
        std::cerr << "journal_test: " << error.what() << '\n';
        return 1;
    }
}
