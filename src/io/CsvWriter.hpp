#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace blc {

// Buffered, append-only CSV writer (RFC 4180 quoting, '\n' line endings).
//
// Rows are assembled in a reusable in-memory buffer and handed to the OS only when the
// buffer passes `flush_threshold` bytes (or on flush()/destruction), so the hot path does
// no syscalls and no per-row heap allocation. Write failures throw std::runtime_error:
// silently losing audit rows is worse than stopping.
class CsvWriter {
public:
    static constexpr std::size_t kDefaultFlushThreshold = 1U << 20;  // 1 MiB

    // Opens (truncates) `path`. Throws std::runtime_error if the file cannot be opened.
    explicit CsvWriter(const std::string& path,
                       std::size_t flush_threshold = kDefaultFlushThreshold);
    ~CsvWriter();

    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;
    CsvWriter(CsvWriter&&) = delete;
    CsvWriter& operator=(CsvWriter&&) = delete;

    // Text field; quoted and escaped only when it contains ',', '"', '\r' or '\n'.
    void field(std::string_view value);
    void field(const char* value) { field(std::string_view(value)); }
    // Single character field (e.g. order book `type` / `side`).
    void field(char value);
    // Any integer type, formatted with std::to_chars (locale-independent, no allocation).
    template <typename Int, typename = std::enable_if_t<std::is_integral_v<Int> &&
                                                        !std::is_same_v<Int, char> &&
                                                        !std::is_same_v<Int, bool>>>
    void field(Int value) {
        char tmp[24];
        const auto res = std::to_chars(tmp, tmp + sizeof(tmp), value);
        separator();
        buffer_.append(tmp, static_cast<std::size_t>(res.ptr - tmp));
    }

    // Terminates the current row and flushes if the buffer is over the threshold.
    void end_row();

    // Appends a complete, already CSV-formatted row (no trailing newline) and ends it.
    // For callers that format a fixed schema into a stack buffer in one pass.
    void write_row(std::string_view formatted) {
        buffer_.append(formatted);
        row_has_fields_ = true;
        end_row();
    }

    // Pushes buffered bytes to the OS (fwrite + fflush).
    void flush();

    const std::string& path() const noexcept { return path_; }
    std::uint64_t rows_written() const noexcept { return rows_; }

    // Exposed for tests and for callers that build fields themselves.
    static bool needs_quoting(std::string_view value) noexcept;

private:
    void separator() {
        if (row_has_fields_) buffer_.push_back(',');
        row_has_fields_ = true;
    }

    struct FileCloser {
        void operator()(std::FILE* f) const noexcept { std::fclose(f); }
    };

    std::string path_;
    std::unique_ptr<std::FILE, FileCloser> file_;
    std::string buffer_;
    std::size_t flush_threshold_;
    std::uint64_t rows_ = 0;
    bool row_has_fields_ = false;
};

}  // namespace blc
