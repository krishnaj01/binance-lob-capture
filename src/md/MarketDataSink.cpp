#include "md/MarketDataSink.hpp"

namespace blc {

MarketDataSink::MarketDataSink(const std::string& path) : out_(path) {
    for (const auto column : kHeader) out_.field(column);
    out_.end_row();
}

void MarketDataSink::write(const EventMeta& meta, std::string_view symbol,
                           std::string_view payload_json) {
    out_.field(meta.recv.sec);
    out_.field(meta.recv.nsec);
    out_.field(to_string(meta.venue));
    out_.field(to_string(meta.kind));
    out_.field(meta.shard_id);
    out_.field(meta.conn_epoch);
    out_.field(meta.conn_seq);
    out_.field(symbol);
    out_.field(payload_json);
    out_.end_row();
}

}  // namespace blc
