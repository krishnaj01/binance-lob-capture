#include "io/CsvReader.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>

namespace blc {

namespace {

constexpr std::size_t kReadChunk = 1U << 16;

// Bytes that end (or are illegal inside) an unquoted field.
constexpr std::array<bool, 256> kUnquotedStop = [] {
    std::array<bool, 256> t{};
    t[static_cast<unsigned char>(',')] = true;
    t[static_cast<unsigned char>('\n')] = true;
    t[static_cast<unsigned char>('\r')] = true;
    t[static_cast<unsigned char>('"')] = true;
    return t;
}();

}  // namespace

CsvReader::CsvReader(const std::string& path)
    : path_(path), file_(std::fopen(path.c_str(), "rb")), buf_(kReadChunk) {
    if (!file_) {
        throw std::runtime_error("cannot open '" + path + "' for reading: " +
                                 std::strerror(errno));
    }
    storage_.reserve(4096);
    ends_.reserve(32);
}

bool CsvReader::refill() {
    len_ = std::fread(buf_.data(), 1, buf_.size(), file_.get());
    pos_ = 0;
    if (len_ == 0 && std::ferror(file_.get())) {
        throw std::runtime_error("read error on '" + path_ + "': " + std::strerror(errno));
    }
    return len_ > 0;
}

void CsvReader::fail(const char* what) const {
    throw CsvParseError(path_ + ":" + std::to_string(line_) + ": " + what);
}

// The field readers scan whole runs of the input buffer (table lookup / memchr) and
// append each run at once, instead of moving one byte at a time through get().
int CsvReader::read_unquoted() {
    for (;;) {
        if (pos_ == len_ && !refill()) return kEof;
        const char* begin = buf_.data() + pos_;
        const char* end = buf_.data() + len_;
        const char* p = begin;
        while (p != end && !kUnquotedStop[static_cast<unsigned char>(*p)]) ++p;
        storage_.append(begin, static_cast<std::size_t>(p - begin));
        pos_ += static_cast<std::size_t>(p - begin);
        if (p != end) {
            ++pos_;
            if (*p == '"') fail("quote inside unquoted field");
            return static_cast<unsigned char>(*p);
        }
    }
}

void CsvReader::read_quoted() {
    for (;;) {
        if (pos_ == len_ && !refill()) fail("unterminated quoted field");
        const char* const base = buf_.data();
        const char* p = base + pos_;
        const char* const end = base + len_;
        const char* run = p;  // start of bytes not yet copied to storage_
        // JSON payloads contain an escaped quote ("") every few bytes, so handle the
        // common cases inline in one pass rather than restarting a search per quote.
        while (p != end) {
            if (*p == '"') {
                if (p + 1 == end) break;  // can't see the next byte yet: slow path below
                if (p[1] == '"') {        // "" -> one literal quote
                    storage_.append(run, static_cast<std::size_t>(p + 1 - run));
                    p += 2;
                    run = p;
                    continue;
                }
                storage_.append(run, static_cast<std::size_t>(p - run));
                pos_ = static_cast<std::size_t>(p + 1 - base);  // past the closing quote
                return;
            }
            if (*p == '\n') ++line_;
            ++p;
        }
        storage_.append(run, static_cast<std::size_t>(p - run));
        pos_ = static_cast<std::size_t>(p - base);
        if (p == end) continue;  // field continues in the next chunk
        ++pos_;                  // quote was the last buffered byte; peek() may refill
        if (peek() == '"') {
            ++pos_;
            storage_.push_back('"');
            continue;
        }
        return;
    }
}

bool CsvReader::next(std::vector<std::string_view>& fields) {
    fields.clear();
    storage_.clear();
    ends_.clear();

    // Skip empty lines between records.
    int c = peek();
    while (c == '\n' || c == '\r') {
        get();
        if (c == '\n') ++line_;
        c = peek();
    }
    if (c == kEof) return false;
    record_line_ = line_;

    for (;;) {  // one iteration per field
        if (peek() == '"') {
            get();
            read_quoted();
            c = get();  // must be a delimiter
        } else {
            c = read_unquoted();
        }
        ends_.push_back(storage_.size());

        if (c == ',') continue;
        if (c == '\r') {
            if (get() != '\n') fail("bare carriage return");
            c = '\n';
        }
        if (c == '\n') ++line_;
        if (c == '\n' || c == kEof) break;
        fail("unexpected character after closing quote");
    }

    std::size_t begin = 0;
    for (const std::size_t end : ends_) {
        fields.emplace_back(storage_.data() + begin, end - begin);
        begin = end;
    }
    return true;
}

}  // namespace blc
