#include "io/CsvReader.hpp"

#include <cerrno>
#include <cstring>

namespace blc {

namespace {
constexpr std::size_t kReadChunk = 1U << 16;
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
        c = get();
        if (c == '"') {
            for (;;) {
                c = get();
                if (c == kEof) fail("unterminated quoted field");
                if (c == '"') {
                    if (peek() == '"') {
                        get();
                        storage_.push_back('"');
                        continue;
                    }
                    break;  // closing quote
                }
                if (c == '\n') ++line_;
                storage_.push_back(static_cast<char>(c));
            }
            c = get();  // must be a delimiter
        } else {
            while (c != ',' && c != '\n' && c != '\r' && c != kEof) {
                if (c == '"') fail("quote inside unquoted field");
                storage_.push_back(static_cast<char>(c));
                c = get();
            }
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
