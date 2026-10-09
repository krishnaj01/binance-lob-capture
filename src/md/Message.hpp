#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "common/Clock.hpp"
#include "common/Types.hpp"

namespace blc {

// One raw WebSocket text frame as handed from the network thread to the processor.
// `payload` is the combined-stream envelope: {"stream":"btcusdt@depth@100ms","data":{...}}.
// The network thread reserves SIMDJSON_PADDING spare capacity so it can be parsed in place.
struct RawFrame {
    Timestamp recv;               // wall clock right after the frame was read
    std::uint32_t shard_id = 0;   // index of the WebSocket connection
    std::uint64_t conn_epoch = 0; // increments on every (re)connect of that shard
    std::uint64_t conn_seq = 0;   // 0-based, monotonic within (shard_id, conn_epoch)
    std::string payload;
};

// Everything in a market-data CSV row except the payload.
struct EventMeta {
    Timestamp recv;
    Venue venue = Venue::Spot;
    StreamKind kind = StreamKind::DepthDiff;
    std::uint32_t shard_id = 0;
    std::uint64_t conn_epoch = 0;
    std::uint64_t conn_seq = 0;
};

}  // namespace blc
