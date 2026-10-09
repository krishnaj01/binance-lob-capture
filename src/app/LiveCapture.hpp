#pragma once

#include "cli/Options.hpp"
#include "common/Metrics.hpp"

namespace blc {

// Live capture: Binance WebSocket(s) -> market-data CSV + order book CSV per symbol.
//
// Threads:
//   * main thread    : boost::asio io_context. All WebSocket clients (one per shard), the
//                      SIGINT/SIGTERM handler, the --duration timer and the periodic
//                      metrics timer. It is the only producer of the SPSC queue.
//   * processor      : pops frames, parses JSON, writes both CSVs (buffered), maintains
//                      the books. Sole owner of the Processor, parser and files.
// The threads share only the SPSC queue (lock-free), the relaxed metrics counters and an
// atomic stop flag; no locks on the per-message path.
//
// Shutdown: signal/duration -> clients close gracefully -> io_context runs out of work
// -> stop flag (release) -> processor drains everything still queued -> files flushed.
// A second SIGINT/SIGTERM stops the io_context immediately.
//
// Returns the process exit code.
int run_live_capture(const Options& opts, Metrics& metrics);

}  // namespace blc
