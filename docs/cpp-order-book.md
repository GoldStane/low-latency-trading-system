# Milestone 2: C++ event model and price-level order book

This milestone implements an in-memory, single-owner component, not a trading
runtime. It has no broker connection, file parser, strategy, or background thread.
The Python LOBSTER analyzer remains independent. Historical normalized CSV inputs
now connect through the separate [Milestone 3 adapter and replay](cpp-historical-replay.md).

## Contract and design decisions

The book belongs to one instrument and stores aggregate quantity at each price.
It does not store individual orders or implement FIFO matching. Prices are
positive integer ticks; quantity is an absolute whole-unit level quantity.
Adapters must perform decimal conversion and choose the instrument's tick size
before constructing events (AGENTS.md §5, §7–8, §11, §33).

ADD requires an absent price and positive quantity. MODIFY requires an existing
price and replaces its quantity with a positive quantity. DELETE requires an
existing price and quantity zero. The side and price identify the level. These
rules match the normalized Python schema; they deliberately do not interpret
LOBSTER order cancellation sizes as remaining price-level quantities.

The initial state is Empty. Call begin_sync(), then supply normalized snapshot
levels through apply(). The adapter must provide a contiguous per-instrument
sequence covering this stream: the first sequence may be any uint64 value and
subsequent sequences must increase by exactly one, without wrapping. Synthetic
sequences alone do not prove source-feed completeness. An adapter must verify
source continuity before publishing normalized events.

finish_sync() requires both sides and best_bid < best_ask, then marks the book
Valid. During Syncing, intermediate crossed levels are allowed but not usable for
trading; finish_sync rejects an unresolved cross. Subsequent updates cannot lock
or cross the book. Deleting the last level on a side is allowed: Valid means
consistent, not necessarily two-sided. Strategies/risk must additionally require
the liquidity they need. There is no trading gate in this milestone.

Invalid events fail closed: clear all levels, enter Invalid, return a BookError,
and retain the last accepted sequence/time for diagnostics. apply() cannot repair
an Invalid book. Restart with begin_sync() and a verified snapshot. clear() resets
levels, sequence, timestamps, error and state to Empty. Calling finish_sync() in
the wrong state also invalidates. Allocation failure invalidates with
ResourceFailure and rethrows the original exception; the caller must handle it.
These choices implement §10, §38 and §49 without silent event repair.

Exchange timestamps must be nonnegative Unix epoch nanoseconds and cannot move
backwards within a synchronization session; equal timestamps are accepted.
Receive timestamps are optional, must be nonnegative when present, and are never
substituted for exchange timestamps. No ordering between the two clock readings
is assumed, and neither is used to measure latency (§6). LOBSTER times since
midnight (including fractional nanoseconds in the downloaded files) cannot be
passed directly: a future adapter needs explicit date/timezone and precision
policy. This milestone does not implement that conversion.

std::map is the initial clarity-first implementation (§2, §11, §30, §41).
Insertion, lookup, modification, and deletion are O(log N) for N levels on a
side. ADD may allocate, DELETE frees a node, and MODIFY reuses it. Best-price
queries read ordered-map endpoints. Memory is O(number of occupied levels).
Clearing on failure is linear in book size. No performance optimization or
concurrency is claimed. Invariant checking scans all levels and is intended for
tests/diagnostics, not automatically on every hot-path update.

## File-by-file explanation

### cpp/common/types.hpp

This small header gives shared names to fixed-width integers: Price and Timestamp
are signed 64-bit; Quantity and InstrumentId are unsigned 32-bit; SequenceNumber
is unsigned 64-bit. Using aliases makes interfaces readable while matching §5.
Aliases are not distinct strong types: the compiler can still mix compatible
integers, so the boundary must validate their meaning. There is no float price.

`#pragma once` prevents repeated inclusion of this header in one translation
unit. `<cstdint>` supplies `std::int64_t`, `std::uint32_t`, and `std::uint64_t`.
`namespace trading { ... }` groups project names so they do not occupy the global
namespace. `using Price = std::int64_t;` introduces an alias, not a new class.
Comments specify units because the integer type alone cannot express them.

### cpp/events/book_update.hpp

This header defines the provider-neutral event and its enums (§7). Side selects
Bid or Ask. UpdateType selects Add, Modify or Delete. MarketDataState records
Empty, Syncing, Valid or Invalid. Stale is deferred: detecting staleness needs a
runtime freshness policy and clock, neither owned by this data structure.

`enum class Side : std::uint8_t` is a scoped enum with a fixed one-byte underlying
integer representation. Write `Side::Bid`, not a bare `Bid`. Enum class prevents
implicit conversion to integers, but explicit casts can still manufacture an
invalid value; apply() therefore validates enums.

`struct BookUpdate` groups the fields as public members. It is a simple aggregate,
so brace initialization supplies members in declaration order. A caller must
initialize every mandatory field; prefer complete brace initialization. The
optional receive time uses `std::optional<Timestamp>`: `std::nullopt` or `{}` means
absent, while an integer constructs a present value. Presence is distinct from a
valid timestamp of zero. `optional` stores its value inline, without heap allocation.

Fixed-width fields do not make the struct a portable serialization format:
padding, endianness, and optional's layout remain implementation-dependent.
Persistence must encode fields explicitly in a future versioned format (§27).

### cpp/order_book/order_book.hpp

This header declares the component's public API and private storage.
PriceLevel returns a price and quantity together. Its C++20 defaulted equality
operator compares both members, which simplifies tests.

OrderBook takes an instrument ID at construction. `explicit` prevents an integer
from implicitly turning into a book. The initializer `: instrument_(instrument)`
sets the member before entering the constructor body. In-class initializers such
as `state_{MarketDataState::Empty}` establish defaults for every new instance.

`public:` exposes operations to callers; `private:` hides the maps and tracking
fields so callers cannot bypass validation. `Levels` abbreviates
`std::map<Price, Quantity>`, which maintains unique keys in ascending order.
The highest bid is therefore at `rbegin()`, and the lowest ask at `begin()`.

`[[nodiscard]]` asks the compiler to warn when an important return value is
ignored, especially an apply() failure. `const` after a member function means it
does not modify the object's logical state. `noexcept` promises not to throw;
apply() lacks that promise because inserting a map node may fail to allocate.
A returned `std::optional<PriceLevel>` is a copy, so callers cannot mutate the
internal book through it. Volume lookup returns zero for a missing level;
occupied levels never have zero quantity. `std::size_t` is the standard container
size type; it is not part of the serialized event model.

BookError distinguishes protocol, lifecycle, and input failures. last_error()
provides lightweight observability; it does not log or persist. Calling apply()
again after invalidation reports NotReady, so callers should capture the original
returned error immediately. last_sequence() means last accepted event, not the
failed event or an exchange order ID.

### cpp/order_book/order_book.cpp

This translation unit contains the method definitions. The `.hpp` describes
what callers may do; the `.cpp` supplies the implementation once for linking.
`OrderBook::apply` uses the scope-resolution operator `::` to define a member
outside the class declaration.

apply() checks lifecycle, instrument, field values, sequence and chronology
before touching a level. The max-sequence check occurs before adding one,
preventing wraparound. `&&` and `||` short-circuit left to right, which also makes
checks such as `optional && *optional < 0` safe. `*optional` accesses its contained
value only after presence is established.

`const BookUpdate&` receives a reference to the event without copying it and
prevents the function from modifying it. `auto& levels = condition ? bids_ :
asks_;` binds a reference to the chosen map; it does not copy that map. The `?:`
operator selects one of two expressions. `find()` returns an iterator, and
`end()` is the not-found sentinel. `found->second` accesses a map entry's quantity;
`first` is the immutable key. A const iterator variable cannot be reassigned,
but may still refer to a mutable map entry. `emplace()` inserts; `erase()` removes.

ADD's `try`/`catch (...)` protects the state if insertion throws. `throw;` rethrows
the original exception after invalidating. Expected input errors instead return
BookError; they do not use exceptions. The cross check occurs after mutation,
but failure clears all levels before control returns. Thus callers never receive
a successful result with a crossed Valid book. This class is not thread-safe:
concurrent access, including reads during writes, requires external ownership
coordination (§30).

`for (const auto& [price, quantity] : bids_)` is a range-based loop with structured
bindings: each map pair is exposed under readable names without copying it.
invariants_hold() verifies positive stored levels, empty maps in Empty/Invalid,
and noncrossing prices when Valid. It intentionally permits incomplete Syncing
state and one-sided Valid state. Sequence rules are tested at each transition,
not reconstructed by this structural check.

### tests/unit/test_order_book.cpp

The test executable uses no external test framework. CHECK prints the failed
expression and source line, then exits with status 1; success exits with status 0.
Unlike standard assert, it remains active in Release builds. The preprocessor
macro uses `#condition` to turn an expression into text and `__LINE__` for its
source line. `do { ... } while (false)` lets the macro behave like one statement.

Helpers create events and a synchronized two-sided book. `std::nullopt` explicitly
omits receive time. `auto` lets the compiler infer a variable's type. The `fails`
lambda (`[](...) { ... }`) is a small callable that checks invalidation and
recovery for multiple error cases. `static_cast` deliberately manufactures invalid
enums and verifies that boundary validation rejects them. `numeric_limits<T>::max()`
tests the largest representable values without magic constants.

Tests cover ADD/MODIFY/DELETE on both sides, replacement quantity, missing and
duplicate levels, best-price changes, empty sides, malformed fields, wrong
instrument, time regression, equal timestamps, duplicate/out-of-order/gapped
sequences, sequence overflow, crossing, snapshot completion, recovery and clear.
A repeated three-event stream checks expected best prices and invariants after
3,000 transitions and compares two independently processed books. These are
component tests, not a claim of C++ CSV adapter or broker integration coverage.
Allocation failure handling is implemented but not fault-injected in this suite.

### benchmarks/order_book_benchmark.cpp

The standalone benchmark initializes a two-sided book and cycles ADD, MODIFY,
DELETE on a third level. It discards 100 warm-up batches, then measures 1,000
batches of 300 events. `std::chrono::steady_clock` is monotonic (§6).
`duration<double, std::nano>` converts elapsed durations to nanoseconds; floating
point is used for timing statistics, never canonical market prices.

The vector reserves sample capacity before measurement. Sorting samples gives
nearest-rank p50/p95/p99. These percentiles describe **batch-mean nanoseconds per
event**, not individual-event tail latency. Throughput uses measured batch time;
setup, sample sorting and printing are excluded. This tiny workload is a
reproducible baseline harness, not a representative deep-book or end-to-end
trading latency claim. Compiler, hardware and workload must accompany comparisons
(§40–41); no optimization was performed in this milestone.

### CMakeLists.txt

The project now enables CXX. `add_library` compiles the book implementation into
a reusable library. `target_include_directories(... PUBLIC cpp)` lets both the
library and its consumers include headers relative to cpp. `target_compile_features`
requests C++20 and propagates that requirement. Extensions are disabled for the
library, and target-scoped warning flags select MSVC or GCC/Clang syntax.

`add_executable` creates the test and optional benchmark programs.
`target_link_libraries` connects them to trading_book and inherits its PUBLIC
requirements. `include(CTest)` provides BUILD_TESTING and test registration.
The existing Python suite remains registered beside the C++ test executable.
ENABLE_SANITIZERS adds AddressSanitizer and UndefinedBehaviorSanitizer compile
and link flags for GCC/Clang, propagated to consumers. Separate build folders
keep Debug, Release and sanitizer settings independent (§43–44).

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
./build-release/order_book_benchmark
```

CMake 3.20+, a C++20 compiler, and Python 3.10+ for the existing tests are needed.
Set BUILD_TESTING=OFF if building only the library without Python tests.

## Minimal usage

```cpp
#include "order_book/order_book.hpp"

trading::OrderBook book{7};
book.begin_sync();
const trading::BookUpdate bid{100, std::nullopt, 7, trading::Side::Bid,
                             10000, 10, trading::UpdateType::Add, 1};
const trading::BookUpdate ask{100, std::nullopt, 7, trading::Side::Ask,
                             10002, 20, trading::UpdateType::Add, 2};
if (book.apply(bid) != trading::BookError::None ||
    book.apply(ask) != trading::BookError::None ||
    book.finish_sync() != trading::BookError::None) {
    // Stop using this book and request a new verified snapshot.
}
// Check state and optional best prices before consuming them.
```

A valid local book alone never authorizes trading: freshness, connection state,
reconciliation, runtime state and risk checks remain later milestones (§22, §25,
§47). No live trading is enabled by this component.
