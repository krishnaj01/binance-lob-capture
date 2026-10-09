#pragma once

#include <string>
#include <string_view>

#include "io/CsvWriter.hpp"
#include "md/Message.hpp"

namespace blc {

// Deliverable A: one row per message, in processing order.
class MarketDataSink {
public:
    static constexpr std::string_view kHeader[] = {
        "recv_tsec", "recv_tnsec", "venue",  "stream_kind", "shard_id",
        "conn_epoch", "conn_seq",  "symbol", "payload_json"};

    explicit MarketDataSink(const std::string& path);

    // `symbol` uppercase; `payload_json` = minified inner Binance `data` object.
    void write(const EventMeta& meta, std::string_view symbol, std::string_view payload_json);

    void flush() { out_.flush(); }
    const std::string& path() const noexcept { return out_.path(); }

private:
    CsvWriter out_;
};

}  // namespace blc
