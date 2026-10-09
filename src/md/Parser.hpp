#pragma once

#include <simdjson.h>

#include <string_view>
#include <vector>

#include "book/BookSync.hpp"
#include "common/Types.hpp"

namespace blc {

// Field extraction from Binance `data` objects (simdjson On-Demand, error codes only:
// malformed input never throws and never invokes UB, it returns a status the caller counts).
//
// Payload shapes handled (classification always comes from the stream name, never `e`):
//   spot depth_diff : {"e":"depthUpdate","E":..,"s":..,"U":..,"u":..,"b":[[p,q]..],"a":[..]}
//   spot depth5     : {"lastUpdateId":..,"bids":[[p,q]..],"asks":[..]}
//   usdm depth_diff : {"e":"depthUpdate",..,"U":..,"u":..,"pu":..,"b":[..],"a":[..]}
//   usdm depth5     : same shape as usdm depth_diff (top 5 only); snapshot id = u
//   trade (both)    : {"e":"trade",..,"p":..,"q":..,"m":<buyer is maker>,..}
enum class ParseStatus : std::uint8_t { Ok, BadJson, BadDecimal };

// "btcusdt@depth@100ms" -> {DepthDiff, "btcusdt"}; also @depth5[@100ms] and @trade.
// Returns false for any other stream (e.g. depth10, aggTrade).
struct StreamName {
    StreamKind kind;
    std::string_view symbol_lower;
};
bool parse_stream_name(std::string_view stream, StreamName& out) noexcept;

// Update ids of a depth_diff or depth5 payload (see DepthIds).
ParseStatus parse_depth_ids(simdjson::ondemand::object& data, Venue venue, StreamKind kind,
                            DepthIds& out) noexcept;

// Bid / ask levels of a depth_diff or depth5 payload, in feed order. Reuses the vectors.
ParseStatus parse_depth_levels(simdjson::ondemand::object& data, Venue venue, StreamKind kind,
                               std::vector<Level>& bids, std::vector<Level>& asks) noexcept;

// Trade aggressor: Binance `m` = "buyer is the market maker", i.e. the SELLER aggressed.
ParseStatus parse_trade(simdjson::ondemand::object& data, bool& buyer_is_maker) noexcept;

}  // namespace blc
