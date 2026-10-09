#include "net/StreamUrl.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace blc {

Endpoint make_endpoint(Venue venue, const std::vector<std::string>& symbols) {
    if (symbols.empty()) throw std::invalid_argument("no symbols for endpoint");
    if (symbols.size() > kMaxSymbolsPerShard) {
        throw std::invalid_argument("too many symbols for one connection (max " +
                                    std::to_string(kMaxSymbolsPerShard) + ")");
    }

    Endpoint ep;
    if (venue == Venue::Spot) {
        ep.host = "stream.binance.com";
        ep.port = "9443";
        ep.target = "/stream?streams=";
    } else {
        ep.host = "fstream.binance.com";
        ep.port = "443";
        ep.target = "/public/stream?streams=";
    }

    bool first = true;
    for (const auto& symbol : symbols) {
        std::string lower;
        lower.reserve(symbol.size());
        for (const char c : symbol) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        for (const char* suffix : {"@depth@100ms", "@depth5@100ms", "@trade"}) {
            if (!first) ep.target.push_back('/');
            first = false;
            ep.target.append(lower).append(suffix);
        }
    }
    if (ep.target.size() > kMaxTargetLength) {
        throw std::invalid_argument("stream URL too long; use fewer symbols per shard");
    }
    return ep;
}

std::vector<std::vector<std::string>> shard_symbols(const std::vector<std::string>& symbols,
                                                    std::size_t per_shard) {
    if (per_shard == 0 || per_shard > kMaxSymbolsPerShard) {
        throw std::invalid_argument("symbols per shard must be in [1, " +
                                    std::to_string(kMaxSymbolsPerShard) + "]");
    }
    std::vector<std::vector<std::string>> shards;
    for (std::size_t i = 0; i < symbols.size(); i += per_shard) {
        const auto end = std::min(symbols.size(), i + per_shard);
        shards.emplace_back(symbols.begin() + static_cast<std::ptrdiff_t>(i),
                            symbols.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return shards;
}

}  // namespace blc
