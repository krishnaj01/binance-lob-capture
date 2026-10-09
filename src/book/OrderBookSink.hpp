#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "book/OrderBook.hpp"
#include "common/Clock.hpp"
#include "io/CsvWriter.hpp"

namespace blc {

// `type` column: event class that produced the row.
enum class RowType : char {
    DepthDiff = 'D',  // applied depthUpdate
    Snapshot = 'S',   // applied depth5 (seed or refresh)
    Trade = 'T',      // trade; annotation only, the book is unchanged
    Reset = 'R',      // book cleared: sequence gap, bad payload or new conn_epoch
};

// `side` column.
enum class RowSide : char { Bid = 'B', Ask = 'S', None = 'N' };

// Deliverable B: exact 26-column header; missing levels are written as price 0, size 0.
class OrderBookSink {
public:
    static constexpr std::string_view kHeader =
        "tsec,tnsec,seqNo,id,type,side,"
        "bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,"
        "ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4";
    static constexpr std::size_t kLevels = 5;

    explicit OrderBookSink(const std::string& path);

    void write(const Timestamp& ts, std::uint64_t seq_no, std::int32_t id, RowType type,
               RowSide side, const OrderBook& book);

    void flush() { out_.flush(); }
    const std::string& path() const noexcept { return out_.path(); }

private:
    CsvWriter out_;
};

}  // namespace blc
