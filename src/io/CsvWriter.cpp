#include "io/CsvWriter.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace blc {

CsvWriter::CsvWriter(const std::string& path, std::size_t flush_threshold)
    : path_(path), file_(std::fopen(path.c_str(), "wb")), flush_threshold_(flush_threshold) {
    if (!file_) {
        throw std::runtime_error("cannot open '" + path + "' for writing: " +
                                 std::strerror(errno));
    }
    // We do our own buffering; disable stdio's to avoid a second copy.
    std::setvbuf(file_.get(), nullptr, _IONBF, 0);
    buffer_.reserve(flush_threshold_ + 64 * 1024);
}

CsvWriter::~CsvWriter() {
    try {
        flush();
    } catch (const std::exception& e) {
        // Destructors must not throw; report and let the FILE* close.
        std::cerr << "CsvWriter: final flush of '" << path_ << "' failed: " << e.what() << '\n';
    }
}

bool CsvWriter::needs_quoting(std::string_view value) noexcept {
    return value.find_first_of(",\"\r\n") != std::string_view::npos;
}

void CsvWriter::field(std::string_view value) {
    separator();
    if (!needs_quoting(value)) {
        buffer_.append(value);
        return;
    }
    buffer_.push_back('"');
    std::size_t start = 0;
    for (std::size_t pos; (pos = value.find('"', start)) != std::string_view::npos;
         start = pos + 1) {
        buffer_.append(value.substr(start, pos + 1 - start));  // includes the quote
        buffer_.push_back('"');                                // ...doubled
    }
    buffer_.append(value.substr(start));
    buffer_.push_back('"');
}

void CsvWriter::field(char value) {
    field(std::string_view(&value, 1));
}

void CsvWriter::end_row() {
    buffer_.push_back('\n');
    row_has_fields_ = false;
    ++rows_;
    if (buffer_.size() >= flush_threshold_) flush();
}

void CsvWriter::flush() {
    if (!buffer_.empty()) {
        const std::size_t written = std::fwrite(buffer_.data(), 1, buffer_.size(), file_.get());
        if (written != buffer_.size()) {
            throw std::runtime_error("short write to '" + path_ + "': " + std::strerror(errno));
        }
        buffer_.clear();  // keeps capacity
    }
    if (std::fflush(file_.get()) != 0) {
        throw std::runtime_error("fflush failed for '" + path_ + "': " + std::strerror(errno));
    }
}

}  // namespace blc
