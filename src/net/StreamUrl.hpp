#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/Types.hpp"

namespace blc {

// One combined-stream WebSocket endpoint, e.g.
//   wss://stream.binance.com:9443/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/...
struct Endpoint {
    std::string host;
    std::string port;
    std::string target;  // path + query

    std::string url() const { return "wss://" + host + ":" + port + target; }
};

// Sanity limits (Binance: max 1024 streams per spot connection, 200 per USD-M connection).
inline constexpr std::size_t kStreamsPerSymbol = 3;
inline constexpr std::size_t kMaxStreamsPerConnection = 200;
inline constexpr std::size_t kMaxSymbolsPerShard = kMaxStreamsPerConnection / kStreamsPerSymbol;
inline constexpr std::size_t kMaxTargetLength = 4096;

// Builds the endpoint subscribing to depth@100ms, depth5@100ms and trade for `symbols`
// (uppercase; lowercased in the URL). Throws std::invalid_argument when a limit is exceeded.
//   spot: stream.binance.com:9443  /stream?streams=...
//   usdm: fstream.binance.com:443  /public/stream?streams=...
Endpoint make_endpoint(Venue venue, const std::vector<std::string>& symbols);

// Splits symbols into consecutive groups of at most `per_shard` (one connection each).
std::vector<std::vector<std::string>> shard_symbols(const std::vector<std::string>& symbols,
                                                    std::size_t per_shard);

}  // namespace blc
