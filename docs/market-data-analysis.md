# Offline book-update analysis: first milestone

Run with Python 3.10+; no third-party dependencies are required:

```sh
python3 scripts/analyze_market_data.py data/sample/trades.csv --tick-size 0.01
```

The CLI reads a local UTF-8 CSV and emits a versioned JSON report to stdout
only after successful validation. Redirect stdout to save a report. Exit status
is 0 for success, 1 for input/data failure, and 2 for invalid CLI arguments.
Errors go to stderr. Shell redirection can create an empty file on failure;
consumers must check the exit status. Input files are never modified.

## Input contract (version 2)

The header must contain exactly these columns, in this order:

```text
exchange_timestamp_ns,instrument_id,sequence,price,quantity,side,update_type
```

- `exchange_timestamp_ns`: nonnegative Unix epoch nanoseconds, bounded by int64.
  This is exchange chronology, not receive time or a latency measurement.
- `instrument_id`: uint32 identifier assigned by the upstream data preparation.
- `sequence`: uint64, contiguous per instrument within this file. The first value
  is arbitrary. Each following value must be exactly the preceding value plus one.
- `price`: positive plain decimal in quote-currency units. It must be exactly
  divisible by the explicitly supplied tick size; the resulting ticks must fit int64.
- `quantity`: whole-unit uint32 absolute level quantity for ADD/MODIFY (positive),
  or zero for DELETE.
- `side`: strict, case-sensitive enum `BID` or `ASK`.
- `update_type`: strict, case-sensitive enum `ADD`, `MODIFY`, or `DELETE`.
  ADD introduces a price level, MODIFY replaces its quantity, and DELETE removes
  the level identified by instrument, side, and price. Quantities are not deltas.

Version 2 requires both new columns; version 1 files are rejected. The research
API now exposes `BookUpdate` and `load_updates` instead of `Trade` and `load_trades`.

Blank lines are ignored by the CSV reader. Whitespace inside fields is not
trimmed. Scientific notation, non-finite values, fractional quantities, missing
fields, and extra columns are rejected. Quoted CSV fields are supported.
Each row represents a price-level book update, not a trade or OHLC bar.
All instruments in a file must use the same tick size; analyze separate files
when tick sizes differ. This initial contract excludes zero/negative prices and
fractional-unit products; extending it requires explicit schema and test changes.

Sequence validation assumes a normalized **book-update per-instrument stream**.
Raw feeds may instead sequence all message types or an entire channel. They need
an explicit adapter and continuity validation under their original semantics;
do not renumber an unverified feed merely to pass this validator. Sequence checks
cannot detect data missing before the first or after the last record, or prove
that input matches an external source. A reset/session boundary needs a separate
input file. Duplicate and out-of-order records are rejected, not deduplicated or sorted.
Timestamps must be nondecreasing per instrument; equal timestamps are valid.
Cross-instrument timestamp ordering is unconstrained.

## Report semantics

Per instrument, report event count, counts by side and update type, first/last
exchange timestamps and sequences, and open/close/high/low **event price** ticks.
All events, including DELETE, contribute to counts and price ranges.

`reported_quantity_sum` sums the supplied absolute quantities; it is neither
executed volume nor current book depth. `quantity_price_ticks_sum` sums event
price ticks times reported quantity. `quantity_weighted_update_price_ticks` is
the ratio of those sums, represented as an exact numerator/denominator object.
DELETE contributes zero weight; a DELETE-only stream has a null weighted price.
These are descriptive update statistics, **not trade VWAP or traded notional**.
The report's tick size is also an exact fraction. Multiply price ticks by tick
size to recover quote-currency price. Python integers prevent aggregate overflow;
JSON consumers must support large integers without float conversion.

This analyzer does not reconstruct a book or validate whether a level already
exists on ADD or exists on MODIFY/DELETE. A file may start mid-session. Snapshot
synchronization, level-transition validity, and crossed-book checks belong to
future book reconstruction; successful analysis does not establish book validity.

Statistics are calculated separately per instrument; no cross-currency notional
is summed. Reports contain no run timestamp or machine-specific timings, so the
same input and tick size produce identical bytes. Empty input is an error.

## Decisions and AGENTS.md references

- Offline Python only (§4): this script has no strategy, networking, broker, or
  order-submission functionality. Production normalization/replay remains future C++ work.
- CSV and standard-library implementation (engineering judgment, §2): explicit,
  inspectable input and zero installation dependencies suit this initial milestone.
- Exact decimal-string conversion at the loader boundary (§5, §7): an immutable
  `BookUpdate` record carries integer prices. `Fraction` avoids precision-context rounding.
- Explicit side/update enums and absolute level quantities (§7, §11, engineering
  judgment): parsing rejects unknown enum values; DELETE requires zero. Report
  schema version 2 avoids labeling book updates as executed trades.
- Separate loader and aggregator (§8, §33): future vendor parsers can target the
  research record without introducing vendor payloads into analysis.
- Preserve input order and fail on discontinuity (§2, §10, §39): no silent repair.
  Passing these checks does not imply a valid live book or permission to trade.
- Single-threaded streaming (§29, §30): memory grows with instrument count rather
  than event count. This is cold-path analysis; no latency claims or benchmarks
  are presented for this initial implementation (§40–41).
- Machine-readable results and actionable failures (§36–38): malformed rows
  include CSV line numbers; chronology errors identify instrument and sequence.
- Synthetic sample and ignored local datasets (§3, §35): no external download,
  credentials, or proprietary dataset is needed.
- CMake/CTest entry point (§43): the initial project has no C++ targets and does
  not require a C++ compiler yet. Tests cover arithmetic, validation, CLI failure
  behavior, and repeatability (§39, §49–50).

Future milestones include book reconstruction, provider adapters, and the C++
event pipeline. The trading runtime is not implemented or enabled by this tool.

## LOBSTER paired files

The downloaded `data/LOBSTER` directory contains AAPL, AMZN, GOOG, INTC,
MSFT message/orderbook pairs for 2012-06-21, each with ten price levels.
Use the separate provider adapter (AGENTS.md §7–8, §33):

```sh
python3 scripts/analyze_market_data.py data/LOBSTER/AAPL_2012-06-21_34200000_57600000_message_10.csv --format lobster
```

The paired orderbook filename is inferred by replacing `_message_` with
`_orderbook_`. For renamed files, pass `--orderbook PATH`. Do not supply
`--tick-size`: LOBSTER integer prices are USD multiplied by 10000, which is
an encoding scale, not a claim about the minimum exchange tick increment.
The adapter reads both headerless files together and fails on unequal row
counts, malformed values, inconsistent book depth, invalid padding, or backwards
timestamps. Same-time events retain file order. Empty input fails.

The local `LOBSTER_SampleFiles_ReadMe.txt` defines the downloaded format:
message columns are seconds since midnight, type, order ID, size, price,
and direction. Direction 1 is BID and -1 is ASK, including the resting-order
side for executions (not aggressor direction). Order IDs are not sequence
numbers. Nanoseconds since midnight are parsed exactly, without inventing
an epoch timestamp or timezone (AGENTS.md §5–6). No source sequence-gap
validation is possible from these files.

Types 1–5 retain submission, partial cancellation, deletion, visible execution,
and hidden execution semantics. Type 6 is counted separately as a cross trade;
type 7 retains halt/quoting/trading status. Neither is counted as an ordinary
order side. Cancellation/deletion size describes removed order quantity, not
remaining absolute price-level quantity. Consequently the adapter does not
coerce messages into the normalized ADD/MODIFY/DELETE level schema.

Every orderbook row describes the book after its corresponding message.
Columns repeat ask price/size then bid price/size. Dummy ask/bid prices
9999999999/-9999999999 with zero sizes are omitted from occupied levels.
Levels must be strictly ordered on each side; locked/crossed snapshots are
counted explicitly, not silently treated as valid trading books (§10–11).
The report includes event counts, execution quantities by category, halt
counts, last snapshot, and the exact event-weighted mean spread over two-sided
snapshots. It is not a time-weighted spread. Missing-sided snapshots are excluded
from that mean. No order-level reconstruction, halt-state gating, or message-to-
snapshot transition proof is performed; this remains offline analysis.

LOBSTER reports use a separate `format: lobster`, `schema_version: 1` contract;
normalized reports retain version 2. Memory is bounded by snapshot depth.
Downloaded data remains ignored by Git; tests use synthetic data (§3, §35, §39).

The downloaded AMZN data includes decimal seconds with more than nine decimal
places. The LOBSTER parser accepts up to 18 fractional digits and preserves them
exactly: timestamp report fields are integer nanoseconds when integral, otherwise
numerator/denominator objects representing fractional nanoseconds. This avoids
silently rounding the supplied data; it does not claim subnanosecond clock accuracy.
