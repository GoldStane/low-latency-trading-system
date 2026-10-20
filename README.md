# low-latency-trading-system
Event-driven C++ trading system with market-data processing, limit order book, strategy and risk engines, order management, exchange simulation, broker integration, and autonomous Linux deployment.

Implemented milestones: offline market-data analysis in Python, and a C++20
event model with a deterministic price-level order book.
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
