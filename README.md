# low-latency-trading-system
An event-driven C++ trading-system project, currently implementing offline
market-data analysis, a price-level order book, deterministic historical replay,
and a generic event pipeline with experimental IBKR market-data connectivity.

Implemented milestones:

- Milestone 1: offline market-data analysis
- Milestone 2: C++ event model + price-level order book
- Milestone 3: C++ historical normalized-data replay
- Milestone 4: generic event pipeline + optional experimental IBKR market data

The C++ trading runtime is planned; no live trading functionality is enabled.

## Analyze sample book updates

Requires Python 3.10+ with no third-party packages.

```sh
python3 scripts/analyze_market_data.py data/sample/trades.csv --tick-size 0.01
```

Loads and validates book-update CSV records and emits deterministic JSON containing
per-instrument event counts by side/update type, event price ranges, and exact
quantity-weighted update prices (not trade VWAP). See
[the input contract and engineering decisions](docs/market-data-analysis.md)
before using your own data.

## Test

```sh
python3 -m unittest discover -s tests/unit -v
```

Or use the project build/test entry point (CMake 3.20+):

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Downloaded LOBSTER data

```sh
python3 scripts/analyze_market_data.py data/LOBSTER/AAPL_2012-06-21_34200000_57600000_message_10.csv --format lobster
```

Automatically reads the paired orderbook file. Preserves LOBSTER order-event
semantics, exact timestamps and price units; reports event counts, executions,
and snapshot spreads. See the LOBSTER section in the input-contract document.

## Milestone 2: C++ order book

The `trading_book` library supports fixed-width event fields, ADD/MODIFY/DELETE,
explicit snapshot synchronization, sequence validation, and fail-closed book
invariants. Read the [file-by-file C++ and syntax guide](docs/cpp-order-book.md)
for interfaces, design decisions, examples, tests, and benchmark instructions.

The CMake build now requires a C++20 compiler. Python analysis still runs independently.

## Milestone 3: C++ historical replay

The streaming `trading_market_data` reader converts normalized CSV prices exactly
to integer ticks. `trading_replay` initializes a book from an explicit snapshot,
finishes synchronization, and applies incremental updates in file order. Invalid
input or book transitions stop replay immediately.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/historical_replay --snapshot data/sample/book_snapshot.csv \
    --updates data/sample/book_updates.csv --tick-size 0.01
```

The deterministic summary reports prices in integer ticks. Read the
[historical replay guide](docs/cpp-historical-replay.md) for the lifecycle,
error handling, limitations, and sanitizer commands. The sample files are synthetic;
the existing multi-instrument `trades.csv` remains for Python analysis.

## Milestone 4: event pipeline and experimental IBKR market data

Historical CSV and IBKR sources share `MarketDataSource` and
`MarketDataSink::on_event()`. Historical BookUpdate events reconstruct the existing
book; IBKR L1 callbacks produce Quote observations without pretending to provide
verified depth. Connection state is distinct from book validity.

Normal builds require no IBKR dependencies. `ENABLE_IBKR` defaults to OFF. The
optional executable is data-only and defaults to the paper TWS port and delayed
data requests. See [architecture](docs/event-pipeline.md),
[SDK setup and connectivity experiment](docs/ibkr-market-data.md), and
[benchmark methodology](docs/benchmarks.md).

This is not a complete trading runtime. Strategies, features/signals, execution,
risk, OMS, portfolio, matching, persistence, reconciliation, and deployment remain
unimplemented. No order-submission functionality is introduced. Real quote receipt
requires a separately authenticated TWS/IB Gateway session and suitable data access;
offline tests do not establish live connectivity.
