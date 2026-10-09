#pragma once

#include <simdjson.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "book/BookSync.hpp"
#include "book/OrderBookSink.hpp"
#include "common/Metrics.hpp"
#include "md/MarketDataSink.hpp"
#include "md/Message.hpp"
#include "md/Parser.hpp"

namespace blc {

// Stable `id` column: FNV-1a 32-bit of "<venue>:<SYMBOL>", masked to a non-negative int32.
// Depends only on venue and symbol, so live and replay runs agree without any config.
std::int32_t instrument_id(Venue venue, std::string_view symbol_upper) noexcept;

// Per-instrument processing state.
struct SymbolState {
    SymbolState(Venue v, std::string_view symbol_upper, std::unique_ptr<MarketDataSink> market_sink,
                std::unique_ptr<OrderBookSink> book_sink);

    Venue venue;
    std::string symbol;        // "BTCUSDT"
    std::string symbol_lower;  // "btcusdt" (as it appears in stream names)
    std::int32_t id;
    BookSync sync;
    std::uint64_t seq_no = 0;  // last emitted order book row

    // Connection the last event for this symbol arrived on (reconnect detection).
    bool seen_connection = false;
    std::uint32_t shard_id = 0;
    std::uint64_t conn_epoch = 0;

    std::unique_ptr<MarketDataSink> market;  // null in replay mode
    std::unique_ptr<OrderBookSink> book_out;
};

// Single-threaded event processor shared by live capture and replay.
//
// Live:   on_frame(envelope) -> parse once -> market-data row -> process(data)
// Replay: on_event(meta, payload_json from CSV)              -> process(data)
//
// Both paths end in the same process(), and the order book row's timestamp is the
// market row's receive timestamp, so replaying a capture reproduces the order book CSV
// byte for byte.
class Processor {
public:
    explicit Processor(Metrics& metrics);

    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;

    SymbolState& add_symbol(Venue venue, std::string_view symbol_upper,
                            std::unique_ptr<MarketDataSink> market_sink,
                            std::unique_ptr<OrderBookSink> book_sink);

    // Live path. Parses the combined-stream envelope, writes the market-data row, then
    // processes it. Frames for unknown streams / symbols are counted as parse errors.
    void on_frame(Venue venue, const RawFrame& frame);

    // Replay path: one market-data row.
    void on_event(SymbolState& state, const EventMeta& meta, std::string_view payload_json);

    void flush();

private:
    void process(SymbolState& st, const EventMeta& meta, simdjson::ondemand::object& data);
    void process_diff(SymbolState& st, const EventMeta& meta, simdjson::ondemand::object& data);
    void process_depth5(SymbolState& st, const EventMeta& meta, simdjson::ondemand::object& data);
    void process_trade(SymbolState& st, const EventMeta& meta, simdjson::ondemand::object& data);
    void reset_book(SymbolState& st, const Timestamp& ts);
    void emit(SymbolState& st, const Timestamp& ts, RowType type, RowSide side);
    void count_parse_failure(ParseStatus status) noexcept;
    SymbolState* find_by_stream_symbol(std::string_view symbol_lower) noexcept;

    Metrics& metrics_;
    simdjson::ondemand::parser parser_;
    std::string padded_;    // copy of input that lacks simdjson padding (replay rows)
    std::string minified_;  // payload_json scratch
    std::vector<Level> bids_;
    std::vector<Level> asks_;
    std::vector<std::unique_ptr<SymbolState>> symbols_;  // few symbols: linear scan
};

}  // namespace blc
