#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace blc {

class CsvParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Streaming RFC 4180 CSV reader used by replay mode.
//
// - Quoted fields may contain ',', '""' (escaped quote), '\r' and '\n'.
// - Records end with '\n' or "\r\n"; a final record without a newline is accepted.
// - Completely empty lines are skipped.
// - Malformed input (stray quote in an unquoted field, garbage after a closing quote,
//   EOF inside quotes) throws CsvParseError naming the line number.
class CsvReader {
public:
    // Throws std::runtime_error if the file cannot be opened.
    explicit CsvReader(const std::string& path);

    CsvReader(const CsvReader&) = delete;
    CsvReader& operator=(const CsvReader&) = delete;

    // Reads the next record. The views point into internal storage and stay valid only
    // until the next call. Returns false at end of file.
    bool next(std::vector<std::string_view>& fields);

    // 1-based line number where the most recently returned record started.
    std::uint64_t record_line() const noexcept { return record_line_; }

private:
    static constexpr int kEof = -1;

    int get() {
        if (pos_ == len_ && !refill()) return kEof;
        return static_cast<unsigned char>(buf_[pos_++]);
    }
    int peek() {
        if (pos_ == len_ && !refill()) return kEof;
        return static_cast<unsigned char>(buf_[pos_]);
    }
    bool refill();
    [[noreturn]] void fail(const char* what) const;

    struct FileCloser {
        void operator()(std::FILE* f) const noexcept { std::fclose(f); }
    };

    std::string path_;
    std::unique_ptr<std::FILE, FileCloser> file_;
    std::vector<char> buf_;
    std::size_t pos_ = 0;
    std::size_t len_ = 0;

    std::string storage_;            // unescaped field bytes of the current record
    std::vector<std::size_t> ends_;  // end offset of each field in storage_
    std::uint64_t line_ = 1;         // current physical line
    std::uint64_t record_line_ = 0;
};

}  // namespace blc
