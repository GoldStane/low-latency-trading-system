# Milestone 4 implementation and validation

The data-ingestion architecture is implemented. Actual authenticated IBKR quote
receipt remains pending by the user's explicit choice; this is not a claim that
all live acceptance criteria were exercised. No trading functionality was added.

## Files added

- `cpp/events/market_data_event.hpp`
- `cpp/market_data/market_data_source.hpp`
- `cpp/market_data/market_data_source.cpp`
- `cpp/market_data/normalized_csv_source.hpp`
- `cpp/market_data/normalized_csv_source.cpp`
- `cpp/runtime/order_book_market_data_handler.hpp`
- `cpp/runtime/order_book_market_data_handler.cpp`
- `cpp/market_data/ibkr/quote_translator.hpp`
- `cpp/market_data/ibkr/quote_translator.cpp`
- `cpp/market_data/ibkr/ibkr_source.hpp`
- `cpp/market_data/ibkr/ibkr_source.cpp`
- `cpp/market_data/ibkr/main.cpp`
- `cmake/ibkr-sdk/CMakeLists.txt`
- `benchmarks/event_pipeline_benchmark.cpp`
- `tests/integration/test_event_pipeline.cpp`
- `tests/integration/test_ibkr_cli.cmake`
- `tests/unit/test_ibkr_translation.cpp`
- `tests/unit/test_ibkr_sdk.cpp`
- `docs/event-pipeline.md`
- `docs/ibkr-market-data.md`
- `docs/benchmarks.md`
- `docs/milestone-4-validation.md`

## Files modified

- `CMakeLists.txt`: core targets, tests, benchmark, optional external IBKR SDK linkage.
- `cpp/replay/historical_replay.hpp` and `.cpp`: compose generic source and handler,
  preserving the existing public interface, summary, and error context.
- `README.md`: implemented milestone and remaining scope.
- `docs/cpp-historical-replay.md`: updated internal architecture references.

The pre-existing user modification to the output wrapping in
`benchmarks/order_book_benchmark.cpp` was preserved; one trailing space was
removed, and the full tree passes `git diff --check`. The parser, BookUpdate
definition, order-book implementation, and previous tests were not changed.

## Architecture and semantics

Both sources deliver `MarketDataEvent` through `MarketDataSink::on_event()`.
The CSV source emits snapshot boundaries and unchanged BookUpdate values. The
provider-neutral handler owns the book and surfaces its original BookError.
The IBKR source emits Quote observations and source connection changes through
the same boundary; its provider SDK remains behind a private implementation.
ConnectionState is independent from MarketDataState.

IBKR watchlist callbacks do not prove sequenced absolute-depth changes, so they
are never fabricated into ADD/MODIFY/DELETE. Quote holds independent optional
price/quantity fields, per-field receive times, confirmed data mode, and absent
exchange time. Prices become integer ticks within a fixed 1e-6 tick tolerance;
quantities are exact whole uint32 units. See the [architecture](event-pipeline.md)
and [IBKR semantics/API/build guide](ibkr-market-data.md).

Used API functionality: connect/disconnect, API-ready handshake, contract-details
request and callbacks, requested/confirmed market-data mode, market-data
subscription, bid/ask price and size callbacks, subscription cancellation, and
error/connection callbacks. No order-related requests or account requests exist.

The benchmark measures 300,000 individually timed calls after 30,000 warm-up
calls, separately for dispatch and dispatch plus apply. It uses steady_clock,
nearest-rank p50/p95/p99, and events divided by summed timed intervals. Setup,
printing, sorting, sample storage, and parsing are outside timed intervals.
The preserved book benchmark measures batch means. See [methodology](benchmarks.md).

## Executed validation

Before changes, the existing five CTest entries passed. After changes, normal
Debug and Release builds and the ASan/UBSan build each pass all seven entries,
including the unchanged Python suite. Commands executed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build-release -j 4
ctest --test-dir build-release --output-on-failure
./build-release/order_book_benchmark
./build-release/event_pipeline_benchmark

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize -j 4
ctest --test-dir build-sanitize --output-on-failure
```

The current official IBKR SDK 10.50.02, Protobuf 3.12.4, and Intel Decimal 2.0 U5
were obtained and built outside the repository under `/tmp`. The optional target
compiled and linked with AppleClang 21 on macOS ARM64. The external SDK library
was built in Release, and the adapter/CLI were built in Debug:

```sh
cmake -S . -B build-ibkr -DCMAKE_BUILD_TYPE=Debug -DENABLE_IBKR=ON \
  -DIBKR_API_ROOT=/tmp/llts-ibkr-sdk \
  -DIBKR_API_LIBRARY=/tmp/llts-sdk-build/libtwsapi.a \
  -DIBKR_EXTRA_LIBRARIES=/tmp/IntelRDFPMathLib20U5/LIBRARY/libbid.a \
  -DCMAKE_PREFIX_PATH=/tmp/llts-protobuf-install
cmake --build build-ibkr -j 4
ctest --test-dir build-ibkr --output-on-failure
```

All nine optional-build tests passed, including SDK Decimal conversion, lifecycle
without a connection, and CLI pre-connect validation. Repeating that configuration
in `build-ibkr-sanitize` with `-DENABLE_SANITIZERS=ON` also passed all nine tests
without ASan/UBSan reports. Project code was instrumented; external dependency
libraries were not rebuilt with sanitizers. No test requires TWS or IB Gateway.
The standalone SDK CMake helper was also configured successfully against these
dependencies; the SDK build recipe used during validation compiled the same source
and generated Protobuf files.

`cmake -S . -B build-ibkr-missing -DENABLE_IBKR=ON` correctly failed with a useful
missing-SDK diagnostic. `ibkr_market_data --help` succeeded. A manual connection
attempt to localhost:7497 reported Connecting then Failed and exited nonzero:
there was no paper TWS/Gateway listener. No quote receipt is claimed. Reproduce the
live check later with the command in the IBKR guide.

## Deferred work

Authenticated quote receipt and live Ctrl+C verification are pending. L2/depth,
automatic reconnect/resynchronization, ongoing freshness monitoring, fully bounded
nonblocking connect, broader security types, and replay pacing are future work.
Strategies, signals/features, risk, OMS, order execution, matching, portfolio,
account reconciliation, persistence, deployment, and additional concurrency were
intentionally excluded. IBKR's socket reader is the only background thread.
