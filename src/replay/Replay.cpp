#include "replay/Replay.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "io/CsvReader.hpp"
#include "md/MarketDataSink.hpp"
#include "pipeline/OutputFiles.hpp"
#include "pipeline/Processor.hpp"

namespace blc {

namespace {

enum Column : std::size_t {
    kRecvTsec, kRecvTnsec, kVenue, kStreamKind, kShardId, kConnEpoch, kConnSeq, kSymbol,
    kPayload, kColumnCount
};

template <typename Int>
Int parse_int(std::string_view s, const char* column, const CsvReader& reader) {
    Int value{};
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        throw CsvParseError("line " + std::to_string(reader.record_line()) + ": bad " + column +
                            " '" + std::string(s) + "'");
    }
    return value;
}

[[noreturn]] void bad_row(const CsvReader& reader, const std::string& what) {
    throw CsvParseError("line " + std::to_string(reader.record_line()) + ": " + what);
}

}  // namespace

ReplayResult run_replay(const std::string& market_csv, const std::string& output_dir,
                        Metrics& metrics) {
    CsvReader reader(market_csv);
    std::vector<std::string_view> fields;

    if (!reader.next(fields) || fields.size() != std::size(MarketDataSink::kHeader) ||
        !std::equal(fields.begin(), fields.end(), std::begin(MarketDataSink::kHeader))) {
        throw CsvParseError(market_csv + ": missing or unexpected market data header");
    }

    std::filesystem::create_directories(output_dir);
    ReplayResult result;
    result.orderbook_path = orderbook_path_for(output_dir, market_csv);

    // The sink exists before the first row so a header-only input still yields a header.
    auto book_sink = std::make_unique<OrderBookSink>(result.orderbook_path);
    Processor processor(metrics);
    SymbolState* state = nullptr;

    while (reader.next(fields)) {
        if (fields.size() != kColumnCount) {
            bad_row(reader, "expected " + std::to_string(kColumnCount) + " columns, got " +
                                std::to_string(fields.size()));
        }
        EventMeta meta;
        meta.recv.sec = parse_int<std::int64_t>(fields[kRecvTsec], "recv_tsec", reader);
        meta.recv.nsec = parse_int<std::int32_t>(fields[kRecvTnsec], "recv_tnsec", reader);
        if (meta.recv.nsec < 0 || meta.recv.nsec > 999'999'999) {
            bad_row(reader, "recv_tnsec out of range");
        }
        const auto venue = parse_venue(fields[kVenue]);
        if (!venue) bad_row(reader, "unknown venue '" + std::string(fields[kVenue]) + "'");
        const auto kind = parse_stream_kind(fields[kStreamKind]);
        if (!kind) bad_row(reader, "unknown stream_kind '" + std::string(fields[kStreamKind]) + "'");
        meta.venue = *venue;
        meta.kind = *kind;
        meta.shard_id = parse_int<std::uint32_t>(fields[kShardId], "shard_id", reader);
        meta.conn_epoch = parse_int<std::uint64_t>(fields[kConnEpoch], "conn_epoch", reader);
        meta.conn_seq = parse_int<std::uint64_t>(fields[kConnSeq], "conn_seq", reader);
        const std::string_view symbol = fields[kSymbol];

        if (state == nullptr) {
            if (symbol.empty()) bad_row(reader, "empty symbol");
            state = &processor.add_symbol(meta.venue, symbol, nullptr, std::move(book_sink));
        } else if (state->venue != meta.venue || state->symbol != symbol) {
            bad_row(reader, "replay input must contain a single venue+symbol");
        }

        processor.on_event(*state, meta, fields[kPayload]);
        ++result.rows_read;
    }
    processor.flush();
    return result;
}

}  // namespace blc
