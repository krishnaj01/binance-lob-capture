#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "book/OrderBook.hpp"
#include "common/Types.hpp"

namespace blc {

// Update-id fields of a Binance depth event.
//   first     = U  (first update id in the event)
//   last      = u  (final update id in the event; for spot depth5: lastUpdateId)
//   prev_last = pu (USD-M only: `u` of the previous diff event; 0 on spot)
struct DepthIds {
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    std::uint64_t prev_last = 0;
};

enum class DiffAction : std::uint8_t {
    Apply,     // contiguous with the book: apply it
    Stale,     // u <= last applied id: already reflected in the book, skip
    Unsynced,  // no snapshot yet (start / after a gap / new connection): skip
    Gap,       // at least one update is missing: the book can no longer be trusted
};

enum class SnapshotAction : std::uint8_t {
    Apply,    // refreshed / seeded the book
    Stale,    // older than what the book already reflects: ignored
    Invalid,  // levels not strictly ordered or non-positive qty: ignored
};

// Keeps one OrderBook consistent with Binance's update-id sequence.
//
// State machine (documented in the README):
//   Unsynced --depth5(L)--> Synced(last_id = L)
//   Synced   --diff-->      Stale   if u < last_id, or u == last_id after a diff
//                           Apply   if contiguous   (last_id = u)
//                           Gap     otherwise       -> book cleared, Unsynced
//   Synced   --depth5(L)--> Apply   if L >= last_id (top refreshed, last_id = L)
//                           Stale   if L <  last_id (snapshot older than the book)
//   any      --invalidate()--> book cleared, Unsynced   (new conn_epoch, bad payload)
//
// Contiguity rules (Binance docs, applied relative to last_id):
//   spot:  U <= last_id + 1                (U == last_id + 1 in steady state; U < that
//                                            only for the first diff straddling a snapshot)
//   usdm:  pu == last_id  ||  U <= last_id (chained via pu; second clause = straddling)
// Re-applying a straddling diff is safe because diff quantities are absolute.
//
// A diff with u == last_id right after a snapshot is APPLIED, not skipped: on the live
// feed depth5(L) and the diff ending at u == L describe the same 100 ms tick, and depth5
// usually arrives first. That diff agrees with the snapshot on the top levels and is the
// only source of the deeper-level changes in that tick, so skipping it would silently
// reduce the book to "depth5 only".
class BookSync {
public:
    static constexpr std::size_t kSnapshotDepth = 5;

    explicit BookSync(Venue venue, std::size_t max_levels = OrderBook::kDefaultMaxLevels);

    // Pure decision; does not modify state.
    DiffAction check_diff(const DepthIds& ids) const noexcept;

    // Applies levels of a diff for which check_diff() returned Apply.
    void apply_diff(const DepthIds& ids, const std::vector<Level>& bids,
                    const std::vector<Level>& asks);

    // depth5 snapshot (best first) with update id `last_id`.
    SnapshotAction apply_snapshot(std::uint64_t last_id, const std::vector<Level>& bids,
                                  const std::vector<Level>& asks);

    // Drops the book and waits for the next snapshot.
    void invalidate() noexcept;

    bool synced() const noexcept { return synced_; }
    std::uint64_t last_update_id() const noexcept { return last_id_; }
    const OrderBook& book() const noexcept { return book_; }

private:
    Venue venue_;
    OrderBook book_;
    std::uint64_t last_id_ = 0;
    bool synced_ = false;
    bool last_was_snapshot_ = false;  // last_id_ came from depth5, not from a diff
};

}  // namespace blc
