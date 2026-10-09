#include "book/BookSync.hpp"

namespace blc {

BookSync::BookSync(Venue venue, std::size_t max_levels) : venue_(venue), book_(max_levels) {}

DiffAction BookSync::check_diff(const DepthIds& ids) const noexcept {
    if (!synced_) return DiffAction::Unsynced;
    if (ids.last < last_id_ || (ids.last == last_id_ && !last_was_snapshot_)) {
        return DiffAction::Stale;
    }
    const bool contiguous = venue_ == Venue::Spot
                                ? ids.first <= last_id_ + 1
                                : ids.prev_last == last_id_ || ids.first <= last_id_;
    return contiguous ? DiffAction::Apply : DiffAction::Gap;
}

void BookSync::apply_diff(const DepthIds& ids, const std::vector<Level>& bids,
                          const std::vector<Level>& asks) {
    for (const Level& l : bids) book_.set_level(Side::Bid, l.price, l.qty);
    for (const Level& l : asks) book_.set_level(Side::Ask, l.price, l.qty);
    last_id_ = ids.last;
    last_was_snapshot_ = false;
}

SnapshotAction BookSync::apply_snapshot(std::uint64_t last_id, const std::vector<Level>& bids,
                                        const std::vector<Level>& asks) {
    if (synced_ && last_id < last_id_) return SnapshotAction::Stale;
    if (!OrderBook::is_valid_snapshot(Side::Bid, bids) ||
        !OrderBook::is_valid_snapshot(Side::Ask, asks)) {
        return SnapshotAction::Invalid;
    }
    if (!synced_) book_.clear();  // drop anything left from before the gap / reconnect
    book_.replace_top(Side::Bid, bids, kSnapshotDepth);
    book_.replace_top(Side::Ask, asks, kSnapshotDepth);
    last_id_ = last_id;
    last_was_snapshot_ = true;
    synced_ = true;
    return SnapshotAction::Apply;
}

void BookSync::invalidate() noexcept {
    book_.clear();
    last_id_ = 0;
    synced_ = false;
    last_was_snapshot_ = false;
}

}  // namespace blc
