#include "book/OrderBookSink.hpp"

#include <array>
#include <charconv>

namespace blc {

OrderBookSink::OrderBookSink(const std::string& path) : out_(path) {
    // The header is a fixed contract; write it verbatim rather than field-by-field.
    std::size_t start = 0;
    for (std::size_t comma; (comma = kHeader.find(',', start)) != std::string_view::npos;
         start = comma + 1) {
        out_.field(kHeader.substr(start, comma - start));
    }
    out_.field(kHeader.substr(start));
    out_.end_row();
}

void OrderBookSink::write(const Timestamp& ts, std::uint64_t seq_no, std::int32_t id,
                          RowType type, RowSide side, const OrderBook& book) {
    std::array<Level, kLevels> bids{};
    std::array<Level, kLevels> asks{};
    book.top(Side::Bid, bids.data(), kLevels);
    book.top(Side::Ask, asks.data(), kLevels);

    // The whole row is integers and two letters, none of which need CSV quoting: format
    // it into one stack buffer and hand it to the writer in one go.
    constexpr std::size_t kMaxRow = 26 * 22;  // 26 fields x (20 digits + sign + comma)
    char row[kMaxRow + 64];
    char* p = row;
    char* const end = row + sizeof(row);
    const auto put = [&p, end](auto value) {
        p = std::to_chars(p, end - 1, value).ptr;  // always leaves room for the comma
        *p++ = ',';
    };
    put(ts.sec);
    put(ts.nsec);
    put(seq_no);
    put(id);
    *p++ = static_cast<char>(type);
    *p++ = ',';
    *p++ = static_cast<char>(side);
    *p++ = ',';
    for (const Level& l : bids) put(l.price);
    for (const Level& l : bids) put(l.qty);
    for (const Level& l : asks) put(l.price);
    for (const Level& l : asks) put(l.qty);
    out_.write_row(std::string_view(row, static_cast<std::size_t>(p - row - 1)));  // drop last ','
}

}  // namespace blc
