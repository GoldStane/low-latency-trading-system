# Market-data event pipeline

This milestone adds data ingestion and synchronous dispatch, not a trading runtime.

```text
NormalizedCsvMarketDataSource -- BookUpdate, SnapshotBegin/End --+
                                                              |
IbkrMarketDataSource ---------- Quote -------------------------+--> MarketDataSink::on_event
                                                              |          |
Both sources ----------------- ConnectionStateChanged ---------+          +--> quote printer
                                                                         +--> OrderBookMarketDataHandler
                                                                              (BookUpdate only)
```

`MarketDataSource` provides `start()`, `poll()`, `stop()` and `state()`. `poll()`
performs bounded work and returns whether more work may arrive, not whether an
event was emitted. Delivery is an ordinary virtual sink call carrying a small
`std::variant`; there is no queue, generic message bus, scheduler, or worker in
the core. Sinks outlive sources. Each source has one owner, and calls from a sink
back into its source are prohibited. References to events are valid only during
the callback; consumers retaining an event copy it.

Sources are one-shot sessions. Start a new object with fresh streams or a new
connection to recover; neither source silently resumes after failure. `start()`
begins connection, `poll()` advances work, and `stop()` cancels. Destruction also stops
an unfinished source, so an abandoned partial replay cannot leave a Valid result. CSV polls read at
most one row/event plus snapshot control notifications; IBKR polls drain available
SDK messages after a 100 ms signal wait. Its SDK connection call is synchronous
and subject to OS TCP connection timeouts. No hard real-time scheduling is claimed.

Connectivity is separate from book validity:

- Disconnected → Connecting → Connected describes the source connection.
- Connected → Completed means clean historical EOF. Stopping an already completed
  historical source preserves its final book. A live source never reports Completed.
- Connecting/Connected → Failed means parsing, delivery, or API failure. Failure is
  terminal until explicit stop; there is no automatic reconnect.
- If a sink rejects the Completed notification, the source also becomes Failed.
- Stop transitions to Disconnected and is idempotent.
- Empty/Syncing/Valid/Invalid belong to `OrderBook`. A Connected source does not
  establish that a book is Valid, fresh, complete, or safe for trading.

The CSV source borrows both streams and uses the unchanged `NormalizedCsvReader`
and exact `DecimalTickSize` conversion. The first snapshot row identifies the
instrument and emits SnapshotBegin. Snapshot rows must be ADD. SnapshotEnd is
emitted at explicit snapshot EOF; only a successful sink return allows reading
increments. File order, original sequences, and timestamps remain untouched.
There is no pacing, sorting, repair, deduplication, or synthetic receive time.

The book handler owns an optional single-instrument book. It translates snapshot
boundaries into `begin_sync()`/`finish_sync()`, and passes BookUpdate directly to
`apply()`. `BookDispatchError` preserves the original BookError. It deliberately
ignores Quote: L1 observations cannot establish verified price-level depth.
Consumers select behavior by normalized event type, never by provider identity.

A source marks itself Failed and rethrows the original exception before reading
another record. The handler preserves an Invalid book and its original diagnostic;
otherwise Failed/Disconnected clears partial state to Empty. Connecting starts a
fresh book session. Completed preserves a successful historical result. Consumers
of live Quote must likewise discard cached fields on Failed/Disconnected; no
quote cache is maintained by the core handler.

`HistoricalReplay` remains the convenient facade used by the existing CLI. It
composes source and handler, retains the same summary and const book accessor,
and wraps book/parser failures with the same phase and physical line information.
All earlier tests remain unchanged. New integration tests also compare the generic
path with an independent direct parse/apply reference.

The IBKR boundary is explained in [IBKR market data](ibkr-market-data.md).
The core event header contains no SDK, CSV, symbol, or network types. The only
concurrency is the IBKR SDK reader; API callbacks dispatch on the poll owner.
Socket error notifications can arrive on the reader thread: a small atomic flag
and mutex-protected diagnostic transfer them to the owner without invoking sinks
or throwing across that thread. See the IBKR guide for its limitations.
