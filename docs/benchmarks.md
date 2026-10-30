# Benchmark methodology

Build and run optimized binaries:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
./build-release/order_book_benchmark
./build-release/event_pipeline_benchmark
```

The original order-book benchmark is preserved, including its metric names and
sampling design. A sample is the elapsed time of 300 ADD/MODIFY/DELETE operations divided
by 300. It discards 100 warm-up batches and retains 1,000 batches. Its
`batch_mean_ns_per_event` p50/p95/p99 describe batch means, **not individual-event
p99**. This distinction matters because batching hides short stalls.

The event-pipeline benchmark has two separate cases:

- `dispatch`: normalized variant event → virtual sink → observable checksum.
- `dispatch_book`: the same virtual boundary → variant selection → book apply.

Each case constructs all events and allocates sample storage before measurement,
warms up with 30,000 events, and records 300,000 individual calls. One latency
sample is the steady-clock interval immediately around one `sink.on_event(event)`.
The book starts with a bid at 100 and an ask at 102; events repeatedly add, modify,
and delete a bid at 101. Sequence numbers include warm-up and continue without
reset. Book initialization, event construction, sample storage, sorting, and
console output are outside each timed interval. Map allocation/deallocation
caused by apply is intentionally included.

Output uses deterministic metric names, including
`dispatch.individual_event_ns.p50`, `.p95`, `.p99`, and
`.timed_region_events_per_second`, with corresponding `dispatch_book` names.
Percentiles use nearest ranks in sorted samples. Throughput is event count divided
by the sum of individually timed regions, not wall-clock throughput of the entire
harness. A checksum and final book checks keep work observable.

Steady-clock reading overhead and clock resolution are included and not subtracted.
Very small samples can quantize to zero or one clock tick. Virtual-call overhead
may be smaller than this instrumentation overhead; do not subtract the two cases
to claim an accurate dispatch cost. CPU scheduling, allocator caches, optimization,
and workload order also affect results. Repeat runs and report CPU, OS, compiler,
build flags, and workload with comparisons. These are a small in-memory baseline,
not representative deep-book performance or market-to-order latency.

No CSV parsing, file I/O, network time, exchange time, or console time is included.
No performance thresholds are part of the tests. No optimization was made, and
no expected performance numbers are hardcoded here.
