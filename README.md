# binance-lob-capture

A C++17 program that connects to Binance public WebSocket streams (`depth@100ms`,
`depth5@100ms`, `trade`) for configured symbols and produces two files per symbol:

- **A. Market-data CSV**: one row per received message, in processing order (an audit log).
- **B. Order-book CSV**: a local limit order book maintained from that stream, written as
  one top-5 snapshot row after every processed event.

A **replay mode** regenerates B from A with no network access. On every capture tested,
the replayed order book is **byte-identical** to the live one.

| | |
|---|---|
| Language | C++17 (`-std=c++17`, no GNU extensions) |
| Compiler | GCC 13.3 (Ubuntu 24.04); CMake ≥ 3.22 so Ubuntu 22.04 / GCC 11–12 also configure |
| Build | CMake + Ninja (or Makefiles), Release by default |
| Venues | `spot` (stream.binance.com:9443) and `usdm` (fstream.binance.com `/public`) |

---

## Contents

1. [Quick start](#1-quick-start)
2. [Build](#2-build)
3. [Run](#3-run)
4. [Output formats](#4-output-formats)
5. [Scaling and timestamp policies](#5-scaling-and-timestamp-policies)
6. [Order book maintenance, sequencing, gaps and reconnects](#6-order-book-maintenance-sequencing-gaps-and-reconnects)
7. [Design: threading, I/O and memory](#7-design-threading-io-and-memory)
8. [Metrics](#8-metrics)
9. [Performance](#9-performance)
10. [Testing and verification](#10-testing-and-verification)
11. [Sample run](#11-sample-run)
12. [Project layout](#12-project-layout)
13. [Limitations and possible extensions](#13-limitations-and-possible-extensions)
14. [How to Submit on GitHub](#how-to-submit-on-github)

---

## 1. Quick start

```bash
sudo apt-get install -y cmake ninja-build g++ libssl-dev libboost-all-dev zlib1g-dev git

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Live: 2 minutes of spot BTCUSDT -> ./output/
./build/binance_capture --venue spot --symbols BTCUSDT --duration 120 --output-dir ./output

# Replay (no network): regenerate the order book from the market-data file
./build/binance_capture --replay ./output/market_data_spot_BTCUSDT_<date>.csv --output-dir ./replayed
cmp ./output/*_orderbook.csv ./replayed/*_orderbook.csv && echo identical

# Tests
cmake --build build --target tests && ./build/tests/unit_tests
```

## 2. Build

### Dependencies

| Dependency | Version used | Source | Used for |
|---|---|---|---|
| GCC | 13.3.0 | system | compiler |
| CMake | 3.28 (min 3.22) | system | build |
| Ninja | any | system | build (optional; Makefiles work too) |
| Boost | 1.83 (min 1.74) | `libboost-all-dev` | Beast (WebSocket), Asio (I/O, TLS, signals, timers), program_options |
| OpenSSL | 3.0.13 | `libssl-dev` | TLS |
| simdjson | v3.10.1 (pinned) | CMake `FetchContent` | JSON parsing |
| GoogleTest | v1.15.2 (pinned) | CMake `FetchContent` | unit tests |

simdjson and GoogleTest are downloaded at configure time from GitHub at fixed tags. Their
headers are marked as system includes, so our strict warning flags apply only to our code.

### Commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # Release is also the default
cmake --build build --parallel
```

| CMake option | Default | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | `-O3 -DNDEBUG` |
| `BLC_WARNINGS_AS_ERRORS` | `OFF` | adds `-Werror` |
| `BLC_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `BLC_BUILD_TESTS` | `ON` | builds `build/tests/unit_tests` (target alias: `tests`) |

Warning flags on all project code: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual`. The tree
builds with **zero warnings**, and `-DBLC_WARNINGS_AS_ERRORS=ON` is used when developing.
Two places needed care:
- OpenSSL's `SSL_set_tlsext_host_name` macro contains a C-style cast. We call the
  underlying `SSL_ctrl` instead, so `-Wold-style-cast` can stay on.
- An `snprintf` that GCC could not prove safe was replaced with explicit formatting.

Executables: `build/binance_capture`, `build/bench_replay`, `build/tests/unit_tests`.

## 3. Run

```
binance_capture --venue spot|usdm --symbols SYM[,SYM...] [options]     # live
binance_capture --replay <market_data_*.csv> [--output-dir DIR]        # replay
```

| Option | Default | Meaning |
|---|---|---|
| `--venue` | `spot` | `spot` or `usdm` (USD-M futures) |
| `--symbols` | — | Comma-separated, case-insensitive, e.g. `BTCUSDT,ethusdt`. Normalised to uppercase; duplicates removed; `[A-Za-z0-9]{1,20}`; at most 100 |
| `--output-dir` | `./output` | Created if missing |
| `--duration` | `0` | Seconds to capture, then shut down gracefully (`0` = until Ctrl+C / SIGTERM) |
| `--symbols-per-shard` | `20` | Symbols per WebSocket connection (3 streams each; 1–66) |
| `--metrics-interval` | `10` | Seconds between metrics lines on stderr (`0` = only at exit) |
| `--busy-poll` | off | Processor thread spins instead of sleeping when idle (see §9) |
| `--replay` | — | Replay mode: read this market-data CSV and write its order book CSV |
| `--debug-disconnect-every` | `0` | **Testing aid**: drop every connection every N seconds to exercise reconnects |

Examples:

```bash
./build/binance_capture --venue spot --symbols BTCUSDT --output-dir ./output                  # until Ctrl+C
./build/binance_capture --venue spot --symbols BTCUSDT,ETHUSDT --duration 300 --output-dir ./output
./build/binance_capture --venue usdm --symbols BTCUSDT --duration 120 --output-dir ./output
./build/binance_capture --replay output/market_data_spot_BTCUSDT_2026-10-10.csv --output-dir ./replayed
```

Exit code: `0` on clean shutdown; `1` on an invalid argument, a malformed replay input, or
an output write failure.

Ctrl+C / SIGTERM:
1. WebSocket connections are closed gracefully.
2. Every frame already received is processed.
3. Both CSVs are flushed.
4. A final metrics line is printed.

A second signal forces an immediate stop.

## 4. Output formats

All CSVs are UTF-8 and comma-separated. Fields containing `,`, `"`, CR or LF are
double-quoted, with inner `"` written as `""` (RFC 4180). Lines end in LF. The replay
reader also accepts CRLF.

### File naming (one pair of files per venue and symbol)

```
<output-dir>/market_data_<venue>_<SYMBOL>_<YYYY-MM-DD>.csv
<output-dir>/market_data_<venue>_<SYMBOL>_<YYYY-MM-DD>_orderbook.csv
```

The date is the **UTC** date when the process started. A run that crosses midnight keeps
writing to the same files, which keeps one run as one replayable file. Replay writes
`<input stem>_orderbook.csv`, so the live and replayed order book files have the same name.

### A. Market-data CSV

Header (exact, fixed order):

```
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
```

| Column | Meaning |
|---|---|
| `recv_tsec`, `recv_tnsec` | Receive time as integer seconds + nanoseconds in `[0, 999999999]` (see §5) |
| `venue` | `spot` or `usdm` |
| `stream_kind` | `depth_diff`, `depth5` or `trade` (strings, not numbers) |
| `shard_id` | Index of the WebSocket connection (0 with one connection) |
| `conn_epoch` | 0 for a shard's first connection, then **+1 on every reconnect** |
| `conn_seq` | 0-based message counter, monotonic within `(shard_id, conn_epoch)` |
| `symbol` | Uppercase, e.g. `BTCUSDT` |
| `payload_json` | The inner Binance `data` object, minified to one line, CSV-escaped |

There is one row per message, written in **processing order**: exactly the order the
order book consumer sees. Frames that cannot be attributed to a configured symbol are not
written and are counted in `parse_errors`. These are malformed JSON, an unknown stream, or
a non-object `data`.

### B. Order-book CSV

Header (exact, single line, 26 columns):

```
tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4
```

| Column | Type | Meaning |
|---|---|---|
| `tsec`, `tnsec` | int64 / int32 | The **same** receive timestamp as the market-data row that produced this row |
| `seqNo` | uint64 | 1, 2, 3, … per file; one increment per emitted row |
| `id` | int32 | Instrument id (see below) |
| `type` | char | Event that produced the row (see below) |
| `side` | char | `B` bid, `S` ask, `N` both / neither |
| `bid0..bid4`, `bid_size0..4` | int64 | Best five bids, best first, scaled ×10^8 |
| `ask0..ask4`, `ask_size0..4` | int64 | Best five asks, best first, scaled ×10^8 |

A level that does not exist is written as price `0`, size `0`.

**`id`** is the FNV-1a 32-bit hash of `"<venue>:<SYMBOL>"` masked to a non-negative int32
(`spot:BTCUSDT` → `2014473836`). It is stable across runs and identical in replay, with no
configuration needed.

**`type` / `side` mapping**

| `type` | Emitted when | `side` |
|---|---|---|
| `S` | A `depth5` snapshot was applied (seed or refresh) | `N` |
| `D` | A `depthUpdate` diff was applied | `B` if it changed only bids, `S` if only asks, else `N` |
| `T` | A trade arrived. **Annotation only: trades never modify the book** | Aggressor side: `m=true` (buyer is maker) → `S`; `m=false` → `B` |
| `R` | The book was cleared: sequence gap, unparseable diff, or new `conn_epoch` | `N` (all levels `0`) |

Events that do not change the book produce **no** row. These are a stale or unsynced diff,
or a stale or invalid depth5. They are counted in metrics.

## 5. Scaling and timestamp policies

### Integer scaling

Binance sends prices and quantities as decimal strings. Both are converted to `int64` with
a **fixed scale of 10^8** (8 decimal places) for every symbol and venue:

```
"82590.00000000" -> 8259000000000        "0.00013000" -> 13000        "0" -> 0
```

- The conversion is pure integer string parsing (`parse_decimal`, `constexpr`). No
  `double` is involved anywhere, so it is deterministic and exact.
- It accepts `DIGITS[.DIGITS]`. Digits beyond the 8th decimal are accepted **only if they
  are all `0`** (lossless).
- Anything else is **rejected, never rounded or truncated**: a sign, an exponent,
  whitespace, an empty string, overflow, or a non-zero 9th decimal. Rejections are counted
  in `decimal_errors`.
- Range: up to 92,233,720,368.54775807, far above any Binance price or size.

### Timestamps

The policy is **receive wall-clock time** (the recommended option).
`clock_gettime(CLOCK_REALTIME)` is read by the network thread **immediately after a
complete WebSocket message has been read**, before any parsing or queueing. It is split
into `recv_tsec` / `recv_tnsec`.

The order-book row produced by an event reuses that event's receive timestamp. That is
why replay reproduces the order book exactly. Binance's own event time (`E`, ms) is
preserved inside `payload_json`.

In the 2-minute sample, `recv − E` is ~32 ms median (network path to Binance), and receive
timestamps are non-decreasing across the file.

## 6. Order book maintenance, sequencing, gaps and reconnects

### Stream semantics

| Stream | Meaning | How the book uses it |
|---|---|---|
| `depth@100ms` (`depthUpdate`) | **Differential**: absolute new quantity per changed level; `qty 0` = remove level | Applied level-by-level (upsert / erase) |
| `depth5@100ms` | **Partial snapshot** of the top 5 levels | **Replace semantics** for the top of the book (seed and periodic refresh) |
| `trade` | Individual trades | Not applied to the book; `T` annotation row only |

**depth5 replace semantics.** Take the snapshot's worst (5th) price on a side. Every
existing level at that price or better is deleted and the five snapshot levels are
inserted. Deeper levels are kept. A snapshot with fewer than five levels on a side means
the side has no more, so that whole side is replaced.

**Why trades do not touch the book.** A trade does not say which resting orders were
consumed. Binance reflects the result in the next `depthUpdate`, so applying trades too
would double-count.

### Sequence handling (`U` / `u` / `pu`)

Each symbol has a small state machine (`BookSync`) tracking `last_id`: the last update id
reflected in the book.

```
Unsynced --depth5(L)--> Synced(last_id = L)                      [S row]

Synced --diff(U,u[,pu])-->
    Stale  if u < last_id, or u == last_id after a diff          [no row]
    Apply  if contiguous; last_id = u                            [D row]
    Gap    otherwise -> book cleared, Unsynced                   [R row]

Synced --depth5(L)-->
    Apply  if L >= last_id; top refreshed, last_id = L           [S row]
    Stale  if L <  last_id (older than the book)                 [no row]
```

Contiguity rules:
- **spot:** `U <= last_id + 1`. In steady state `U == last_id + 1`.
- **usdm:** `pu == last_id`, or `U <= last_id` for a diff that straddles a snapshot.
  USD-M ids are not dense; diffs chain through `pu`.

A straddling diff covers ids on both sides of the snapshot. Re-applying it is safe because
diff quantities are absolute.

**The same-tick rule (found in live data).** On spot, `depth5(L)` and the diff ending at
`u == L` come from the same 100 ms tick, and **depth5 usually arrives first**. A naive
`u <= last_id → stale` rule therefore discards *every* diff. The book silently degrades
to "depth5 only", so deep levels are never maintained. That diff describes the same state
as the snapshot: it agrees on the top levels and is the only source of the deeper-level
changes. So a diff with `u == last_id` is **applied when `last_id` came from a snapshot**,
and is stale only when it came from a diff. There is a regression test
(`BookSyncSpot.SameTickDiffAfterSnapshotUpdatesDeeperLevels`). On USD-M the two streams
are not aligned (depth5's `u` falls mid-diff), and the straddle rule covers that case.

### Gaps

A gap means an update was missed, so the book can no longer be trusted. The book is
**cleared and an `R` row is written**, so downstream consumers never read a book that may
be wrong. Diffs are then dropped (`depth_diffs_unsynced`) until the next depth5 re-seeds
the book, normally within ~100 ms. A diff whose levels fail to parse is treated as a gap.
A depth5 that fails to parse is just ignored, because a snapshot can only refresh the book.

### Reconnects and `conn_epoch`

Every reconnect starts a new `conn_epoch`, and `conn_seq` restarts at 0. Messages may have
been missed in between, so the first event of a symbol carrying a new `(shard_id,
conn_epoch)` **clears that symbol's book and writes an `R` row**. The book then re-seeds
from the next depth5, exactly as after a gap. The reset is driven by the epoch value in
the data, not by a socket event, so replay reproduces it.

Reconnect policy:
- Exponential backoff: 0.5 s doubling to a 30 s cap, reset once a frame is received.
- Keep-alive pings, and an idle timeout (no traffic for 20 s → reconnect).
- Binance's daily disconnect is handled the same way.

`samples/spot_BTCUSDT_reconnects/` shows this on real data: epochs 0→1→2, two `R` rows,
0 gaps.

### Validation against Binance's own snapshots

In Python, the book was rebuilt from **diffs alone**, seeded only from the first depth5.
It was then compared with every subsequent depth5 at the same update id, over 290
snapshots of live BTCUSDT.

- **0 quantity mismatches.**
- The only differences (8) were prices the diff-only book had never seen: levels deeper
  than the 5-level seed that moved up into the top 5.

This is exactly what depth5 refresh covers, and why the program uses both streams.

## 7. Design: threading, I/O and memory

```
                     main thread (boost::asio io_context)                       processor thread
  Binance ──TLS──► WsClient shard 0 ─┐                                     ┌─► Processor
  Binance ──TLS──► WsClient shard 1 ─┼─► FrameChannel.filled  (SPSC) ─────┤     parse once (simdjson)
                   signals, timers  ─┘   FrameChannel.recycled (SPSC) ◄───┘     market-data CSV append
                   (duration, metrics)        (empty buffers return)            BookSync / OrderBook
                                                                                order-book CSV append
```

### Threads and synchronisation

- **Main thread** runs one `io_context`: every WebSocket client (one per shard), the
  SIGINT/SIGTERM handler, the `--duration` timer and the metrics timer. All network code
  is single-threaded, so it needs no locks or strands.
- **Processor thread** owns the parser, the books and both CSV writers exclusively.
- The threads share only:
  - two **lock-free SPSC ring buffers**. Each has a single producer and single consumer;
    all shards run on the one network thread, so SPSC still holds with several shards.
    Acquire/release indices sit on separate cache lines, with cached copies of the
    other side's index.
  - relaxed atomic metrics counters.
  - an atomic stop flag.

  There is no mutex on the per-message path.

### Back-pressure

The network thread **never blocks** on disk or on the processor. If `filled` is full
(16,384 frames, ≈ minutes of traffic), the frame is dropped and counted in
`queue_full_drops`. The processor then sees an update-id gap and resyncs. That's the right
trade for a feed: blocking would stall the socket and get the connection dropped by
Binance. No drops have occurred in any run.

### File I/O

Each CSV has its own 1 MiB in-memory buffer. Rows are appended without syscalls, and the
buffer is written with `fwrite` + `fflush` whenever it passes 1 MiB, and at shutdown.
stdio's own buffering is disabled, to avoid a second copy. A write failure throws: the
processor logs it and shuts the capture down (exit code 1) rather than silently losing
audit rows.

### Parse once

On the live path, each frame is parsed once with simdjson On-Demand:
1. Read `stream`, which classifies the message by stream suffix, never by `e`.
2. Take the raw `data` object, minify it into a reused buffer, and write it to the
   market-data CSV.
3. `rewind()` the same document (the stage-1 index is reused) and walk `data` for the book.

Price levels are parsed only for diffs that pass the sequence check.

### Memory

| Item | Approach |
|---|---|
| **Frame buffers** | Recycled. `filled` carries frames to the processor, and `recycled` carries the buffers back. The network thread takes a returned buffer before copying the next frame in. Simdjson padding is reserved, so frames are parsed in place. |
| **Pool size** | The pool grows only to the maximum number of frames in flight. Steady state does no allocation in our code. |
| **TLS read buffers** | Asio enables `SSL_MODE_RELEASE_BUFFERS`, which frees OpenSSL's record buffers after every read and allocates them again on the next. It is cleared per connection: ~34 KB per connection buys an allocation-free TLS read path. |
| **Order book** | Per side, a `std::vector<Level>` sorted worst→best, so the **best level is at the back**. Top-of-book inserts, updates and erases touch the tail (small `memmove`). Lookups are a binary search over contiguous memory. The top-5 read is O(5). |
| **Book vs `std::map`** | A map would allocate a node per level and chase pointers on every lookup. The trade-off is O(distance-from-top) for inserts deep in the book, which are rare. Each side is capped at 5,000 levels; beyond that the worst levels are dropped, and they cannot affect the top 5. |

### Shutdown and lifetimes

- Clients are `shared_ptr`-owned. Every async handler holds a strong reference, so no
  handler can outlive its client.
- `stop()` cancels each pending operation. Each operation's completion path reports once
  through `on_stopped`, and when all clients have stopped, the signal wait is cancelled.
- The `io_context` then runs out of work. The main thread sets the stop flag
  (release); the processor drains `filled`, flushes and exits; the main thread joins it.
- After a connection drops, the dead stream is destroyed immediately. Its keep-alive
  timer would otherwise keep `io_context` alive, and an early version showed a 10 s
  shutdown delay because of it.

Valgrind reports "All heap blocks were freed" for both live and replay runs.

### Security and limits

- **TLS:** certificate verification is on: system CA store, `verify_peer`, RFC-compliant
  host name verification, SNI, TLS ≥ 1.2. Only public market-data streams are used, so
  there are no credentials anywhere.
- **Limits:**
  - WebSocket messages over 1 MiB are rejected.
  - At most 100 symbols, 66 per connection (≤ 200 streams).
  - Stream URL ≤ 4 KiB.
  - Book ≤ 5,000 levels per side.
  - Bounded queues.

## 8. Metrics

Printed to stderr every `--metrics-interval` seconds and at exit, as `name=value` pairs:

| Counter | Meaning |
|---|---|
| `frames_received`, `bytes_received` | WebSocket messages / payload bytes read |
| `reconnects`, `connect_failures` | Successful reconnections / failed attempts |
| `queue_full_drops` | Frames dropped because the processor fell behind |
| `messages_processed` | Frames processed (live) or rows replayed |
| `parse_errors` | Malformed JSON, unknown stream/symbol, invalid snapshot |
| `decimal_errors` | Price/qty strings rejected by the fixed-point parser |
| `depth_diffs_applied` / `_stale` / `_unsynced` | Diff outcomes (see §6) |
| `depth5_applied` / `depth5_stale` | Snapshot outcomes |
| `trades` | Trade messages |
| `sequence_gaps` | Update-id gaps detected (book reset) |
| `market_rows_written`, `book_rows_written` | Rows written to each CSV |
| `pipeline_latency_ns_avg` / `_max` | Live only: receive timestamp → frame fully processed (queue wait + parse + book + CSV append) |
| `queue_depth` | Frames waiting in the hand-off queue |

## 9. Performance

Measured on an Intel Core i5-13500H, Linux 7.0, GCC 13.3 `-O3`. The portable x86-64
build was used, with no `-march`.

| Measure | Result | How |
|---|---|---|
| Replay throughput | **~0.68 M msgs/s (~1.47 µs/msg)**, ~260 MiB/s of CSV | `./build/bench_replay samples/spot_BTCUSDT_2min/market_data_spot_BTCUSDT_2026-10-10.csv 200` |
| Live pipeline latency (default) | avg **~58 µs**, max ~0.2 ms | `pipeline_latency_ns_*`; dominated by the processor's 50 µs idle sleep |
| Live pipeline latency (`--busy-poll`) | avg **~3.4 µs**, max ~76 µs | Same metric. Costs one core at 100% |
| Heap allocations, replay | **~0 per message** (279 in total for a whole replay) | `valgrind` |
| Heap allocations, live | **~0.84 per frame**, all inside Beast/OpenSSL (down from 2.3) | `valgrind`, difference between an 8 s and a 28 s run |

Live load (a few hundred messages/s for several symbols) is about four orders of
magnitude below replay throughput, so the processor is idle almost all the time.

**Where the CPU goes in replay** (callgrind):

| Share | Code | Note |
|---|---|---|
| ~21% | Formatting order-book rows | 26 `std::to_chars` per row into one stack buffer, one append |
| ~20% | Parsing price levels | simdjson + `parse_decimal` |
| ~19% | Un-escaping the JSON payload column | Replay only |
| The rest | memcpy, simdjson stage 1, book updates | Book updates ~2–3% |

**Measured and rejected:**
- *simdjson `raw_json_token()` for prices*, to avoid the unescape copy: 8% **more**
  instructions. Reverted.
- *Asio `recycling_allocator` on the read handler*: no change in allocations per frame.
  Reverted.
- *`-march=native`*: lets simdjson's On-Demand code use AVX2 instead of its generic path,
  but gave no measurable gain (−1.5% instructions, wall time within noise). Not worth the
  loss of portability.

**Idle strategy.** By default the processor sleeps 50 µs when the queue is empty. That
uses ~0% CPU when idle, and the wait does not affect recorded timestamps, which are taken
on the network thread. `--busy-poll` spins with `pause` instead, for the lowest hand-off
latency.

## 10. Testing and verification

```bash
cmake --build build --target tests && ./build/tests/unit_tests     # 50 tests
```

| Suite | Covers |
|---|---|
| `Decimal` | Scaling, zero forms, 1e-8, exact INT64 limit, overflow, malformed inputs, `constexpr` evaluation |
| `CsvTest` | RFC 4180 escaping, round-trip with quotes/commas/newlines, CRLF, line numbers, malformed input |
| `SpscQueue` | FIFO, full/empty, buffer recycling, 1M-element two-thread ordering |
| `OrderBook` | Ordering, qty-0 removal, depth5 replace semantics, thin snapshots, level cap, snapshot validation |
| `BookSyncSpot` / `BookSyncUsdm` | Stale, straddle, same-tick, gaps, `pu` chaining, stale/invalid snapshots, invalidation |
| `Parser` | Stream names, spot/usdm diff and depth5 payloads, trades, malformed payloads |
| `StreamUrl` | Spot/usdm URLs, sharding, limits |
| `ProcessorTest` | Live frames → both CSVs (exact rows), then **replay must be byte-identical** |
| `Replay.GoldenSpotScenario` | 13-message scenario: seed, straddle, qty-0, stale, trade, gap, unsynced, thin re-seed, stale snapshot, new epoch, bad decimal. Expected output derived independently in Python |
| `Replay.*` | Bad header, short rows, bad venue, out-of-range nsec, mixed symbols, header-only input |

Beyond unit tests, each of these was run on the final code:

| Check | Result |
|---|---|
| ASan + UBSan (`-DBLC_ENABLE_SANITIZERS=ON`), unit tests and live capture with reconnects | Clean |
| ThreadSanitizer, unit tests and a live capture with 2 shards, `--busy-poll` and reconnects | Clean. Run with `setarch -R` on recent kernels |
| Valgrind memcheck, replay and live with a reconnect | 0 errors, all heap blocks freed |
| Live → replay byte identity | Every capture, including reconnects, spot and USD-M |
| Diff-only book vs Binance depth5 | 0 quantity mismatches (§6) |

Reviewer checks:

```bash
cmake -B build && cmake --build build                                        # build from clean
./build/binance_capture --venue spot --symbols BTCUSDT --duration 300        # live 300 s capture
awk -F',' 'NR==2{print NF}' output/*_orderbook.csv                           # -> 26
```

The brief's credential scan (`git grep -i` for the three keywords it lists) returns
nothing. This README avoids spelling the keywords out so it does not match the scan
itself.

## 11. Sample run

| Directory | What | Rows (market / book) | Result |
|---|---|---|---|
| `samples/spot_BTCUSDT_2min/` | 120 s spot BTCUSDT | 2,947 / 2,947 (1,176 S, 1,176 D, 595 T) | 0 gaps, 0 parse errors, 0 drops |
| `samples/spot_BTCUSDT_reconnects/` | 30 s with `--debug-disconnect-every 10` | 568 / 570 (adds 2 R) | epochs 0→1→2, 0 gaps |

Each directory contains both CSVs and `capture.log` (stderr, including metrics). To check
a sample without network:

```bash
./build/binance_capture --replay samples/spot_BTCUSDT_2min/market_data_spot_BTCUSDT_2026-10-10.csv --output-dir /tmp/replayed
cmp samples/spot_BTCUSDT_2min/market_data_spot_BTCUSDT_2026-10-10_orderbook.csv \
    /tmp/replayed/market_data_spot_BTCUSDT_2026-10-10_orderbook.csv && echo identical
```

First rows of the 2-minute sample (truncated):

```
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
1791602063,815223331,spot,depth5,0,0,0,BTCUSDT,"{""lastUpdateId"":101261452055,""bids"":[[""82590.00000000"",""5.77423000""],...

tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,...
1791602063,815223331,1,2014473836,S,N,8259000000000,8258999000000,8258998000000,8258997000000,8258993000000,577423000,...
```

## 12. Project layout

```
CMakeLists.txt, cmake/Dependencies.cmake
src/
  main.cpp                  entry point: live or replay
  app/LiveCapture.*         thread wiring, signals, timers, shutdown
  cli/Options.*             command line parsing and validation
  net/WsClient.*            TLS WebSocket client, reconnect, conn_epoch / conn_seq
  net/StreamUrl.*           combined-stream URLs, sharding, limits
  md/FrameChannel.hpp       SPSC hand-off + buffer pool
  md/Parser.*               simdjson payload parsing
  md/MarketDataSink.*       Deliverable A writer
  pipeline/Processor.*      shared live/replay event processing
  pipeline/OutputFiles.hpp  file naming
  book/OrderBook.*          price-level book (sorted vectors)
  book/BookSync.*           U/u/pu sequencing state machine
  book/OrderBookSink.*      Deliverable B writer
  replay/Replay.*           market-data CSV -> order book CSV
  io/CsvWriter.*, io/CsvReader.*   buffered RFC 4180 writer / streaming reader
  common/                   Decimal, Clock, SpscQueue, Metrics, Log, Types
tests/                      GoogleTest suites + fixtures/ (golden replay input / expected output)
tools/bench_replay.cpp      replay throughput benchmark
samples/                    captured sample runs
```

## 13. Limitations and possible extensions

- **Depth beyond the top 5 comes from diffs only.** There is no REST `/depth` snapshot, so
  levels deeper than the top 5 are known only once a diff touches them. depth5 refresh
  keeps the published top 5 correct every 100 ms.
  - This was a deliberate choice: replay stays a pure function of the market-data CSV.
  - Extension: fetch a REST snapshot on (re)sync and record it in the market-data CSV as an
    extra documented `stream_kind`, so replay stays network-free.
- **One venue per process.** Run two processes to capture spot and USD-M at the same time.
- **The forced-exit path is untested.** A second signal sent milliseconds after the first
  is merged into one by the OS, so this path is just `io_context::stop()`.
- **Replay accepts one venue+symbol per file**, which is what live capture writes.

---

## How to Submit on GitHub

### 1. Initialize and push your repository

```bash
# From your project root
git init
git add .
git commit -m "Initial submission: Binance WebSocket capture + LOB"

# Create a new repo on GitHub (via web UI or CLI), then:
git remote add origin https://github.com/<your-username>/binance-lob-capture.git
git branch -M main
git push -u origin main
```

### 2. Make sure `.gitignore` excludes build artifacts and credentials

```gitignore
build/
*.csv
*.o
*.a
*.so
.env
config/
```

(This repository's `.gitignore` also re-includes `tests/fixtures/*.csv` and
`samples/**/*.csv`, and ignores `*.pem` and `*.key`.)

> **Never commit API keys, private keys or credentials.** All configuration should use
> environment variables or a `.env.example` template (no real values). This project needs
> none: it uses only public market-data streams.

### 3. Tag your submission

```bash
git tag -a v1.0.0 -m "Submission v1.0.0"
git push origin v1.0.0
```
