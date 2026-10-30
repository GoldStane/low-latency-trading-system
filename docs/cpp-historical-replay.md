# Milestone 3: deterministic C++ historical replay

This milestone connects historical normalized CSV to the existing C++20
`BookUpdate` and `OrderBook`. It is a synchronous, single-instrument,
as-fast-as-possible replay tool. It does not authorize or implement trading.
The event types, book implementation, Python analyzers, existing tests, and
multi-instrument Python sample are unchanged.

The current implementation routes replay through the [generic event pipeline](event-pipeline.md).
The observable contract and CLI below are preserved. Snapshot serialization now
belongs to `NormalizedCsvMarketDataSource`, while the provider-neutral
`OrderBookMarketDataHandler` owns book application and synchronization.

## Files and boundaries

- `cpp/market_data/normalized_csv_reader.hpp/.cpp`: `DecimalTickSize`,
  `CsvError`, and `NormalizedCsvReader`. Validates serialized values and streams
  one event at a time. It intentionally does not check sequence continuity,
  timestamp ordering, instrument consistency, or level existence.
- `cpp/replay/historical_replay.hpp/.cpp`: `HistoricalReplay`, `ReplaySummary`,
  and `ReplayError`. Composes the CSV source and book handler, preserving summary/error compatibility.
  Book-state validation still belongs to `OrderBook::apply()` and `finish_sync()`.
- `cpp/replay/main.cpp`: `historical_replay` CLI, file ownership and summary output.
- `data/sample/book_snapshot.csv` and `book_updates.csv`: synthetic two-sided,
  multi-level snapshot plus ADD/MODIFY/DELETE updates changing both best prices.
- `tests/unit/test_normalized_csv.cpp`: parser and exact arithmetic tests.
- `tests/integration/test_historical_replay.cpp`: synchronization, transitions,
  failure behavior, recovery through a new session, and determinism tests.
- `tests/integration/test_replay_cli.cmake`: exact CLI output, repeatability,
  argument/file/input failures, and absence of stdout on failure.

CMake provides `trading_market_data`, `trading_runtime_core`, `trading_replay`,
and `historical_replay` alongside the existing `trading_book`. No third-party
parsing dependency or book API addition is required. These boundaries follow
AGENTS.md §5, §7–9 and §49.

## Normalized input contract

Both files have the same ordered header, matching the
[existing normalized schema](market-data-analysis.md):

```text
exchange_timestamp_ns,instrument_id,sequence,price,quantity,side,update_type
```

- Exchange time is nonnegative Unix epoch nanoseconds within signed int64.
  Historical events have `receive_time = std::nullopt`; no reception time is invented.
- Instrument IDs are uint32 and sequences are uint64. Integer text consists
  solely of ASCII digits, with the existing Python limit of 20 characters.
  Leading zeroes are accepted within that limit. Signs and whitespace are rejected.
- Prices and the mandatory tick size are positive plain decimal text matching
  `[0-9]+(\.[0-9]+)?`, at most 64 characters each. Scientific notation, signs,
  whitespace, missing digits, and non-finite values are rejected.
- Quantity is uint32: positive absolute level quantity for ADD/MODIFY and zero
  for DELETE. MODIFY replaces quantity; it does not add a delta.
- Side is exactly `BID` or `ASK`; update type is exactly `ADD`, `MODIFY`, or `DELETE`.

CSV quotes are decoded before validation, including quoted header fields, with
exact header names and order required. LF, CRLF, and CR record endings and an
unterminated final line are supported. Empty data lines are skipped as in Python;
whitespace-only lines and blank required fields are errors. Whitespace is never
trimmed. An empty first line is an invalid header. Embedded commas, quotes, or
newlines cannot occur in any valid field in this schema; malformed quoting fails.

The reader borrows an `std::istream`, reads only the header in its constructor,
and returns one `std::optional<BookUpdate>` from each `next()` call. `nullopt`
means clean EOF; malformed input or I/O failure throws `CsvError` with a one-based
physical line number. Discard a reader after an exception. Header-only files are
valid at the reader layer: replay rejects an empty snapshot but permits zero updates.

A 1024-byte record bound rejects oversized malformed input before buffering an
unbounded line. All valid rows under the field limits fit within this bound.
Reader memory is O(1) in event count, excluding normal stream buffers; replay's
book still requires O(occupied price levels). No whole-file buffering, sorting,
deduplication, sequence repair, or renumbering occurs.

## Exact price conversion

`DecimalTickSize` validates its value once and retains decimal digits and scale.
For price digits P with scale p and tick digits T with scale t, the exact ratio is
`(P * 10^t) / (T * 10^p)`. Common powers of ten are cancelled before division.
Bounded decimal strings represent scaled integers, so intermediate values larger
than uint64 do not overflow or cause otherwise-valid inputs to be rejected.

Decimal long division uses at most nine subtractions per quotient digit. It
checks the signed-int64 limit before accumulating each quotient digit, then
requires a zero remainder and a positive quotient. There is no binary
floating-point conversion, rounding, or truncation:

```text
100.02  / 0.01  = 10002 ticks
100.025 / 0.005 = 20005 ticks
100.025 / 0.01  -> error (not tick aligned)
```

Invalid tick sizes throw `std::invalid_argument` at configuration time; price
conversion errors from CSV rows are wrapped with their line number. The arithmetic
is correctness-first and bounded by the existing 64-character limit, not an
optimized parsing algorithm or a performance claim.

## Replay lifecycle and errors

`HistoricalReplay::run(snapshot_stream, update_stream, tick_size)` starts a fresh
session on every call. The first snapshot row selects the instrument and constructs
the owned book. It calls `begin_sync()`, applies every snapshot row, then calls
`finish_sync()`. Every snapshot row must be ADD. At least one bid and one ask and
a noncrossed final snapshot are required. The first sequence is arbitrary; the
remaining snapshot sequences must be contiguous and timestamps nondecreasing.

Only successful `finish_sync()` allows incremental replay. The first update must
continue the snapshot sequence immediately, and all later events must preserve
the same instrument, sequence continuity, and nondecreasing exchange time.
Sequence arithmetic never wraps at uint64 maximum. Equal timestamps are allowed.
`OrderBook` remains authoritative for duplicate ADD, missing MODIFY/DELETE,
crossing, and synchronization state. Its existing rule allowing a Valid book to
become one-sided after deletions is preserved.

The explicit file boundary is evidence supplied by the caller that the first
file is a complete snapshot. A stream of incremental records cannot establish
that unseen levels are absent, so replay never guesses a snapshot boundary.
Neither file checks can prove external source completeness or detect omissions
before the first or after the last supplied event (§9–10).

Errors stop processing immediately:

- A book error throws `ReplayError` containing the original `BookError`, phase
  (`snapshot` or `updates`), and line. Snapshot completion errors refer to its
  final physical line. The existing fail-closed book behavior clears levels,
  enters Invalid, and retains the last accepted sequence and original error.
- CSV errors are reported with phase and line. Other failures also propagate.
  Any partial book not already Invalid is cleared to Empty, so an interrupted
  replay cannot leave a usable partial result. An empty snapshot has no book.
- Only successful return provides a `ReplaySummary`. `book()` exposes a const
  optional book for results and diagnostics. A later explicit `run()` starts
  over; replay never skips a failing record and resumes the same session.

The CLI writes a concise summary only after success. All failures write to stderr
and exit nonzero. Functional output contains no clock values or measured timings.
`best_bid` and `best_ask` are integer ticks, not currency text; an empty side prints
`none` with quantity zero. Multiplying by the supplied tick size recovers currency
prices. The committed sample produces:

```text
instrument=1
snapshot_events=4
update_events=6
last_sequence=109
bid_levels=2
ask_levels=2
best_bid=10001
best_bid_quantity=25
best_ask=10003
best_ask_quantity=35
state=VALID
```

## Build, run, and verify

Requires CMake 3.20+, C++20, and Python 3.10+ for the existing Python tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/historical_replay --snapshot data/sample/book_snapshot.csv \
    --updates data/sample/book_updates.csv --tick-size 0.01

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
./build-release/order_book_benchmark
```

Tests exercise integer maxima, malformed/overflowing representations, quote and
line handling, exact tick conversion and alignment, stream errors, snapshot
completion, update transitions, fail-closed errors, and two independent replays
with identical observable results. CLI tests also replay the committed files twice.
The existing benchmark measures book operations only. Milestone 4 adds a separate
[in-memory dispatch benchmark](benchmarks.md), without a parsing-throughput or
end-to-end trading latency claim.

## Deferred scope and LOBSTER

Only one instrument and one tick size per session are supported. There is no
clock-based pacing, real-time/scaled/step replay, or latency simulation in this
historical source. Milestone 4 adds a separate experimental IBKR market-data
source; strategy, feature calculation, risk, OMS, matching, accounting, persistence,
deployment, and trading functionality remain deferred.

Raw LOBSTER is deliberately not converted here. Its messages are order-level
events; cancellation sizes are removed quantities rather than absolute remaining
price-level quantities. Order IDs are not feed sequence numbers. Paired orderbook
rows are snapshots after each message, and its timestamps need explicit date,
timezone, and precision policies before becoming epoch nanoseconds. Coercing those
messages directly into this schema would change their meaning. The Python LOBSTER
analyzer remains independent; a dedicated provider/replay adapter is deferred.
Only synthetic sample data is committed.
