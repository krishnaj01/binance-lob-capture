#include "book/OrderBookSink.hpp"

#include <array>

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

    out_.field(ts.sec);
    out_.field(ts.nsec);
    out_.field(seq_no);
    out_.field(id);
    out_.field(static_cast<char>(type));
    out_.field(static_cast<char>(side));
    for (const Level& l : bids) out_.field(l.price);
    for (const Level& l : bids) out_.field(l.qty);
    for (const Level& l : asks) out_.field(l.price);
    for (const Level& l : asks) out_.field(l.qty);
    out_.end_row();
}

}  // namespace blc
