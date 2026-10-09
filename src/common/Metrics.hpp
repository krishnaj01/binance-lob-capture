#pragma once

#include <atomic>
#include <cstdint>
#include <iosfwd>

namespace blc {

// Process-wide counters for reviewers / operators. Each counter has a single writer
// thread; atomics (relaxed) exist only so another thread can read them for reporting.
// Counters written by the network thread and by the processing thread live on separate
// cache lines to avoid false sharing.
struct Metrics {
    using Counter = std::atomic<std::uint64_t>;

    // --- network thread ---
    alignas(64) Counter frames_received{0};
    Counter bytes_received{0};
    Counter reconnects{0};
    Counter connect_failures{0};
    Counter queue_full_drops{0};  // frames dropped because the processor fell behind

    // --- processing thread ---
    alignas(64) Counter messages_processed{0};
    Counter parse_errors{0};        // malformed JSON / unknown stream
    Counter decimal_errors{0};      // price/qty strings that failed fixed-point parsing
    Counter depth_diffs_applied{0};
    Counter depth_diffs_stale{0};   // u <= last applied id: already reflected
    Counter depth_diffs_unsynced{0};  // dropped while waiting for a snapshot
    Counter depth5_applied{0};
    Counter trades{0};
    Counter sequence_gaps{0};
    Counter market_rows_written{0};
    Counter book_rows_written{0};

    static void inc(Counter& c, std::uint64_t n = 1) noexcept {
        c.fetch_add(n, std::memory_order_relaxed);
    }

    // One "name=value" per line.
    void report(std::ostream& os) const;
};

}  // namespace blc
