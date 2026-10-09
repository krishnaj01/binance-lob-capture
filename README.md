# binance-lob-capture

Captures Binance public WebSocket market data (`depth@100ms`, `depth5@100ms`, `trade`) to a
market-data CSV and maintains a local limit order book (LOB), emitting a top-5 snapshot CSV row
after every processed event. A replay mode regenerates the order book CSV from a saved
market-data CSV without any network access.

> Status: work in progress. Sections below are filled in as the implementation lands.

## Toolchain

- Compiler: GCC 13.3 (`g++`), language standard **C++17**
- Build: CMake >= 3.25, Ninja
- Dependencies:
  - Boost 1.83 (Beast, Asio, program_options) and OpenSSL 3 from system packages
  - simdjson and GoogleTest, fetched at configure time via CMake `FetchContent` (pinned tags)

```bash
sudo apt-get install -y cmake ninja-build g++ libssl-dev libboost-all-dev zlib1g-dev
```

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Options:

| Option | Default | Meaning |
|---|---|---|
| `BLC_BUILD_TESTS` | `ON` | Build the GoogleTest suite (`build/tests/unit_tests`) |
| `BLC_WARNINGS_AS_ERRORS` | `OFF` | Add `-Werror` |
| `BLC_ENABLE_SANITIZERS` | `OFF` | Build with AddressSanitizer + UndefinedBehaviorSanitizer |

## Run

_TODO: live capture and replay CLI._

## Tests

```bash
cmake --build build --target unit_tests
./build/tests/unit_tests
```

## Design

_TODO: CSV formats, scaling and timestamp policy, reconnect/gap handling, threading and I/O._
