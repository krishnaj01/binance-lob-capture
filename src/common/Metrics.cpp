#include "common/Metrics.hpp"

#include <ostream>
#include <utility>

namespace blc {

void Metrics::report(std::ostream& os) const {
    const std::pair<const char*, const Counter*> rows[] = {
        {"frames_received", &frames_received},
        {"bytes_received", &bytes_received},
        {"reconnects", &reconnects},
        {"connect_failures", &connect_failures},
        {"queue_full_drops", &queue_full_drops},
        {"messages_processed", &messages_processed},
        {"parse_errors", &parse_errors},
        {"decimal_errors", &decimal_errors},
        {"depth_diffs_applied", &depth_diffs_applied},
        {"depth_diffs_stale", &depth_diffs_stale},
        {"depth_diffs_unsynced", &depth_diffs_unsynced},
        {"depth5_applied", &depth5_applied},
        {"depth5_stale", &depth5_stale},
        {"trades", &trades},
        {"sequence_gaps", &sequence_gaps},
        {"market_rows_written", &market_rows_written},
        {"book_rows_written", &book_rows_written},
        {"pipeline_latency_ns_max", &pipeline_latency_ns_max},
    };
    for (const auto& [name, counter] : rows) {
        os << name << '=' << counter->load(std::memory_order_relaxed) << '\n';
    }
    const auto frames = frames_received.load(std::memory_order_relaxed);
    if (frames > 0) {
        os << "pipeline_latency_ns_avg="
           << pipeline_latency_ns_total.load(std::memory_order_relaxed) / frames << '\n';
    }
}

}  // namespace blc
