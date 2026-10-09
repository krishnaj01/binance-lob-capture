#include "pipeline/Processor.hpp"

#include <cctype>

namespace blc {

namespace ondemand = simdjson::ondemand;

std::int32_t instrument_id(Venue venue, std::string_view symbol_upper) noexcept {
    std::uint32_t h = 2166136261U;
    const auto mix = [&h](std::string_view s) {
        for (const char c : s) {
            h ^= static_cast<unsigned char>(c);
            h *= 16777619U;
        }
    };
    mix(to_string(venue));
    mix(":");
    mix(symbol_upper);
    return static_cast<std::int32_t>(h & 0x7fffffffU);
}

SymbolState::SymbolState(Venue v, std::string_view symbol_upper,
                         std::unique_ptr<MarketDataSink> market_sink,
                         std::unique_ptr<OrderBookSink> book_sink)
    : venue(v),
      symbol(symbol_upper),
      symbol_lower(symbol_upper),
      id(instrument_id(v, symbol_upper)),
      sync(v),
      market(std::move(market_sink)),
      book_out(std::move(book_sink)) {
    for (char& c : symbol_lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

Processor::Processor(Metrics& metrics) : metrics_(metrics) {
    bids_.reserve(1024);
    asks_.reserve(1024);
}

SymbolState& Processor::add_symbol(Venue venue, std::string_view symbol_upper,
                                   std::unique_ptr<MarketDataSink> market_sink,
                                   std::unique_ptr<OrderBookSink> book_sink) {
    symbols_.push_back(std::make_unique<SymbolState>(venue, symbol_upper, std::move(market_sink),
                                                     std::move(book_sink)));
    return *symbols_.back();
}

SymbolState* Processor::find_by_stream_symbol(std::string_view symbol_lower) noexcept {
    for (auto& s : symbols_) {
        if (s->symbol_lower == symbol_lower) return s.get();
    }
    return nullptr;
}

void Processor::on_frame(Venue venue, const RawFrame& frame) {
    // Parse in place when the network thread left enough spare capacity, else copy.
    const char* json = frame.payload.data();
    std::size_t capacity = frame.payload.capacity();
    if (capacity < frame.payload.size() + simdjson::SIMDJSON_PADDING) {
        padded_.reserve(frame.payload.size() + simdjson::SIMDJSON_PADDING);
        padded_.assign(frame.payload);
        json = padded_.data();
        capacity = padded_.capacity();
    }

    ondemand::document doc;
    std::string_view stream;
    StreamName name{};
    if (parser_.iterate(json, frame.payload.size(), capacity).get(doc) ||
        doc["stream"].get_string().get(stream) || !parse_stream_name(stream, name)) {
        Metrics::inc(metrics_.parse_errors);
        return;
    }
    SymbolState* st = find_by_stream_symbol(name.symbol_lower);
    if (st == nullptr || st->venue != venue || !st->market) {
        Metrics::inc(metrics_.parse_errors);
        return;
    }

    // Deliverable A: the inner `data` object, minified. raw_json() only skips over the
    // already-indexed object; the string is not re-tokenised. On-Demand validates values
    // lazily, so reject a non-object here rather than record it as payload_json.
    std::string_view raw;
    if (doc["data"].raw_json().get(raw) || raw.empty() || raw.front() != '{') {
        Metrics::inc(metrics_.parse_errors);
        return;
    }
    minified_.resize(raw.size());
    std::size_t minified_len = 0;
    if (simdjson::minify(raw.data(), raw.size(), minified_.data(), minified_len)) {
        Metrics::inc(metrics_.parse_errors);
        return;
    }
    minified_.resize(minified_len);

    const EventMeta meta{frame.recv,     venue,           name.kind, frame.shard_id,
                         frame.conn_epoch, frame.conn_seq};
    st->market->write(meta, st->symbol, minified_);
    Metrics::inc(metrics_.market_rows_written);

    // Re-walk the same parsed document (stage-1 index is reused) to process `data`.
    doc.rewind();
    ondemand::object data;
    if (doc["data"].get_object().get(data)) {
        Metrics::inc(metrics_.parse_errors);
        return;
    }
    process(*st, meta, data);
}

void Processor::on_event(SymbolState& state, const EventMeta& meta,
                         std::string_view payload_json) {
    padded_.reserve(payload_json.size() + simdjson::SIMDJSON_PADDING);
    padded_.assign(payload_json);

    ondemand::document doc;
    ondemand::object data;
    if (parser_.iterate(padded_.data(), padded_.size(), padded_.capacity()).get(doc) ||
        doc.get_object().get(data)) {
        Metrics::inc(metrics_.parse_errors);
        return;
    }
    process(state, meta, data);
}

void Processor::process(SymbolState& st, const EventMeta& meta, ondemand::object& data) {
    Metrics::inc(metrics_.messages_processed);

    // A new connection may have missed updates: never carry a book across it.
    if (!st.seen_connection) {
        st.seen_connection = true;
    } else if (st.shard_id != meta.shard_id || st.conn_epoch != meta.conn_epoch) {
        reset_book(st, meta.recv);
    }
    st.shard_id = meta.shard_id;
    st.conn_epoch = meta.conn_epoch;

    switch (meta.kind) {
        case StreamKind::DepthDiff: process_diff(st, meta, data); break;
        case StreamKind::Depth5: process_depth5(st, meta, data); break;
        case StreamKind::Trade: process_trade(st, meta, data); break;
    }
}

void Processor::process_diff(SymbolState& st, const EventMeta& meta, ondemand::object& data) {
    DepthIds ids;
    if (const auto status = parse_depth_ids(data, st.venue, meta.kind, ids);
        status != ParseStatus::Ok) {
        count_parse_failure(status);
        if (st.sync.synced()) reset_book(st, meta.recv);  // an update may have been lost
        return;
    }

    switch (st.sync.check_diff(ids)) {
        case DiffAction::Stale: Metrics::inc(metrics_.depth_diffs_stale); return;
        case DiffAction::Unsynced: Metrics::inc(metrics_.depth_diffs_unsynced); return;
        case DiffAction::Gap:
            Metrics::inc(metrics_.sequence_gaps);
            reset_book(st, meta.recv);
            return;
        case DiffAction::Apply: break;
    }

    // Levels are parsed only for diffs that will actually be applied.
    if (const auto status = parse_depth_levels(data, st.venue, meta.kind, bids_, asks_);
        status != ParseStatus::Ok) {
        count_parse_failure(status);
        reset_book(st, meta.recv);
        return;
    }
    st.sync.apply_diff(ids, bids_, asks_);
    Metrics::inc(metrics_.depth_diffs_applied);

    const RowSide side = bids_.empty() == asks_.empty() ? RowSide::None
                         : bids_.empty()                ? RowSide::Ask
                                                        : RowSide::Bid;
    emit(st, meta.recv, RowType::DepthDiff, side);
}

void Processor::process_depth5(SymbolState& st, const EventMeta& meta, ondemand::object& data) {
    DepthIds ids;
    ParseStatus status = parse_depth_ids(data, st.venue, meta.kind, ids);
    if (status == ParseStatus::Ok) {
        status = parse_depth_levels(data, st.venue, meta.kind, bids_, asks_);
    }
    if (status != ParseStatus::Ok) {
        count_parse_failure(status);  // book untouched: a snapshot only ever refreshes
        return;
    }

    switch (st.sync.apply_snapshot(ids.last, bids_, asks_)) {
        case SnapshotAction::Apply:
            Metrics::inc(metrics_.depth5_applied);
            emit(st, meta.recv, RowType::Snapshot, RowSide::None);
            return;
        case SnapshotAction::Stale: Metrics::inc(metrics_.depth5_stale); return;
        case SnapshotAction::Invalid: Metrics::inc(metrics_.parse_errors); return;
    }
}

void Processor::process_trade(SymbolState& st, const EventMeta& meta, ondemand::object& data) {
    bool buyer_is_maker = false;
    if (const auto status = parse_trade(data, buyer_is_maker); status != ParseStatus::Ok) {
        count_parse_failure(status);
        return;
    }
    Metrics::inc(metrics_.trades);
    // Buyer is maker => the seller crossed the spread (sell aggressor).
    emit(st, meta.recv, RowType::Trade, buyer_is_maker ? RowSide::Ask : RowSide::Bid);
}

void Processor::reset_book(SymbolState& st, const Timestamp& ts) {
    st.sync.invalidate();
    emit(st, ts, RowType::Reset, RowSide::None);
}

void Processor::emit(SymbolState& st, const Timestamp& ts, RowType type, RowSide side) {
    st.book_out->write(ts, ++st.seq_no, st.id, type, side, st.sync.book());
    Metrics::inc(metrics_.book_rows_written);
}

void Processor::count_parse_failure(ParseStatus status) noexcept {
    Metrics::inc(status == ParseStatus::BadDecimal ? metrics_.decimal_errors
                                                   : metrics_.parse_errors);
}

void Processor::flush() {
    for (auto& s : symbols_) {
        if (s->market) s->market->flush();
        s->book_out->flush();
    }
}

}  // namespace blc
