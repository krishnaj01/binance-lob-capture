#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "common/Types.hpp"

namespace blc {

// Price-level order book for one instrument.
//
// Each side is a contiguous std::vector<Level> kept sorted from WORST to BEST price, so
// the best level is at the back. Market activity is concentrated near the top of the
// book, and with this layout an insert/erase there only shifts the few elements behind
// it (cheap memmove), lookups are a binary search over cache-friendly memory, and reading
// the top N levels is O(N) from the tail. Compared with std::map (a heap node per level,
// pointer chasing on every lookup) this is faster for the access pattern of a depth feed;
// the trade-off is O(distance-from-top) for deep inserts, which are rare and bounded by
// the per-side level cap.
class OrderBook {
public:
    // Guards against unbounded growth if the feed keeps adding far-from-top levels.
    // When exceeded, the worst levels are dropped (they never affect the top 5).
    static constexpr std::size_t kDefaultMaxLevels = 5000;

    explicit OrderBook(std::size_t max_levels_per_side = kDefaultMaxLevels);

    void clear() noexcept;

    // Diff semantics: absolute quantity at `price`; qty == 0 removes the level.
    void set_level(Side side, std::int64_t price, std::int64_t qty);

    // depth5 semantics: `best_first` is the authoritative top of `side` (best price first,
    // strictly ordered). Existing levels at or better than the snapshot's worst price are
    // replaced; deeper levels are kept. If the snapshot has fewer than `requested_depth`
    // levels, the side has no more levels than that, so the whole side is replaced.
    void replace_top(Side side, const std::vector<Level>& best_first, std::size_t requested_depth);

    // Copies up to `n` best levels (best first) into `out`; returns how many were copied.
    std::size_t top(Side side, Level* out, std::size_t n) const noexcept;

    std::optional<Level> best(Side side) const noexcept;
    std::size_t depth(Side side) const noexcept { return levels(side).size(); }

    // True if `best_first` is strictly best-to-worst for `side` with positive quantities.
    static bool is_valid_snapshot(Side side, const std::vector<Level>& best_first) noexcept;

private:
    static constexpr bool is_better(Side side, std::int64_t a, std::int64_t b) noexcept {
        return side == Side::Bid ? a > b : a < b;
    }

    std::vector<Level>& levels(Side side) noexcept { return side == Side::Bid ? bids_ : asks_; }
    const std::vector<Level>& levels(Side side) const noexcept {
        return side == Side::Bid ? bids_ : asks_;
    }

    // First position whose price is equal to or better than `price`.
    std::vector<Level>::iterator find_not_worse(Side side, std::int64_t price);

    std::vector<Level> bids_;  // ascending price: best (highest) bid at back
    std::vector<Level> asks_;  // descending price: best (lowest) ask at back
    std::size_t max_levels_;
};

}  // namespace blc
