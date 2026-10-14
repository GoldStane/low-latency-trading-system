# AGENTS.md

## 1. Project Scope

This repository implements a **low-latency event-driven electronic trading system**.

The system consists of:

```text
Historical / Live Market Data
            |
            v
     Market Data Adapter
            |
            v
       Event Pipeline
            |
            v
      Limit Order Book
            |
            v
       Feature Engine
            |
            v
      Strategy Engine
            |
            v
        Risk Engine
            |
            v
 Order Management System
            |
            v
      Broker Adapter
            |
            v
          Broker
            |
            v
 Portfolio / Accounting
            |
      +-----+------+
      |            |
      v            v
 Persistence   Monitoring
```

The implementation must support:

- Historical event replay
- Local order-book reconstruction
- Strategy execution
- Pre-trade risk checks
- Simulated exchange execution
- Paper-broker integration
- Portfolio and PnL accounting
- Broker reconciliation
- Persistent event logging
- Failure recovery
- Autonomous Linux operation
- Component and end-to-end latency measurement

Live trading must remain disabled by default.

---

# 2. Engineering Priorities

When making implementation decisions, use this priority order:

1. Correctness
2. Determinism
3. Testability
4. Failure safety
5. Observability
6. Reliability
7. Performance
8. Architectural sophistication

Do not introduce optimization that changes observable behavior without tests proving correctness.

Do not introduce concurrency unless a benchmark or architectural requirement justifies it.

Do not introduce provider-specific types into core trading components.

---

# 3. Repository Structure

Preferred structure:

```text
low-latency-trading-system/
|
+-- AGENTS.md
+-- README.md
+-- CMakeLists.txt
|
+-- cpp/
|   +-- common/
|   +-- events/
|   +-- market_data/
|   +-- order_book/
|   +-- features/
|   +-- strategy/
|   +-- risk/
|   +-- oms/
|   +-- portfolio/
|   +-- broker/
|   +-- matching_engine/
|   +-- replay/
|   +-- persistence/
|   +-- monitoring/
|   +-- runtime/
|
+-- research/
|   +-- notebooks/
|   +-- experiments/
|   +-- backtests/
|
+-- tests/
|   +-- unit/
|   +-- integration/
|   +-- failure/
|
+-- benchmarks/
|
+-- configs/
|
+-- scripts/
|
+-- docs/
|
+-- data/
    +-- sample/
```

Large proprietary datasets, credentials, broker tokens, generated build directories, and runtime journals must not be committed.

---

# 4. Language Boundaries

## C++

C++ is the production/runtime language for:

- Market-data ingestion
- Event normalization
- Event dispatch
- Limit order book
- Feature calculation used by live strategies
- Strategy execution
- Risk checks
- Order management
- Matching engine
- Portfolio accounting
- Broker integration
- Historical replay
- Persistence
- Monitoring
- Benchmarking
- Autonomous runtime

Use modern C++ and CMake.

Prefer:

- RAII
- value semantics
- explicit ownership
- const-correct interfaces
- scoped enums
- fixed-width integer types
- deterministic state machines
- minimal hidden global state

---

## Python

Python is restricted to non-latency-critical workflows such as:

- Offline data preparation
- Research notebooks
- Statistical experiments
- Backtest prototypes
- Visualization
- Post-run analysis

Production strategy logic should not depend on an interactive Python process.

---

# 5. Core Numeric Types

Do not use floating-point values as canonical exchange prices.

Use integer ticks or integer smallest-currency units.

Example:

```cpp
using Price = std::int64_t;
using Quantity = std::uint32_t;
using SequenceNumber = std::uint64_t;
using InstrumentId = std::uint32_t;
using ClientOrderId = std::uint64_t;
using BrokerOrderId = std::uint64_t;
```

Example:

```text
100.01 USD -> 10001 cents
```

All conversions from external decimal representations must occur at adapter boundaries.

Avoid repeated decimal-to-integer conversions in the hot path.

---

# 6. Time Representation

Use a monotonic clock for latency measurements.

Use a wall-clock or exchange timestamp only when event chronology or external reporting requires it.

Do not calculate latency using a clock that can jump backwards.

Maintain distinct concepts when needed:

```text
exchange_timestamp
receive_timestamp
processing_timestamp
send_timestamp
ack_timestamp
fill_timestamp
```

Do not silently substitute one for another.

---

# 7. Normalized Event Model

External feeds and brokers must be converted into internal event types immediately after parsing.

Core modules must not depend on provider-specific payloads.

Typical internal events:

```text
BookUpdate
Trade
Quote
TimerEvent
OrderRequest
OrderAcknowledged
OrderRejected
OrderCancelled
Fill
ConnectionStateChanged
MarketDataStateChanged
SystemStateChanged
```

Conceptual market-data event:

```cpp
struct BookUpdate {
    Timestamp exchange_time;
    Timestamp receive_time;
    InstrumentId instrument;
    Side side;
    Price price;
    Quantity quantity;
    UpdateType type;
    SequenceNumber sequence;
};
```

Events used by deterministic replay must contain enough information to reproduce the original state transition.

---

# 8. Adapter Interfaces

Provider-specific logic belongs behind interfaces.

Required abstraction boundaries:

```text
MarketDataAdapter
BrokerAdapter
PersistenceAdapter
MonitoringSink
```

Conceptually:

```cpp
class MarketDataAdapter {
public:
    virtual ~MarketDataAdapter() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void subscribe(InstrumentId instrument) = 0;
};
```

```cpp
class BrokerAdapter {
public:
    virtual ~BrokerAdapter() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void submit(const OrderRequest& order) = 0;
    virtual void cancel(ClientOrderId id) = 0;
};
```

Exact interfaces may evolve, but provider-specific API objects must not leak into:

- Strategy
- Risk engine
- OMS state definitions
- Portfolio
- Matching engine
- Core event model

---

# 9. Historical and Live Data Compatibility

Historical and live sources must normalize into the same internal market-data events.

Conceptually:

```text
Historical File -> Historical Adapter --+
                                         |
                                         v
                                   Normalized Events
                                         |
Live Feed -------> Live Adapter ---------+
                                         |
                                         v
                                   Trading Engine
```

A strategy should not need separate implementations for historical replay and live execution.

Replay speed must be configurable independently of event timestamps.

Supported replay modes may include:

```text
as-fast-as-possible
real-time
scaled-time
step-by-step
```

---

# 10. Market Data State

The trading engine must explicitly track feed validity.

Recommended states:

```cpp
enum class MarketDataState {
    Empty,
    Syncing,
    Valid,
    Stale,
    Invalid
};
```

New trading must be blocked when market data is:

```text
Empty
Syncing
Stale
Invalid
```

A data gap, sequence discontinuity, provider reset, corrupted snapshot, or excessive staleness must invalidate or degrade the relevant book.

Recovery flow:

```text
detect invalid state
        |
disable new trading
        |
clear/invalidate affected local state
        |
request snapshot or reconnect
        |
rebuild state
        |
resume incremental events
        |
verify consistency
        |
mark VALID
```

Do not continue trading on a book whose consistency cannot be proven.

---

# 11. Limit Order Book

The order book must support at minimum:

```text
update_bid(price, quantity)
update_ask(price, quantity)

best_bid()
best_ask()

bid_volume(price)
ask_volume(price)

clear()
state()
```

Required invariants for a valid non-crossed market:

```text
best_bid < best_ask
```

unless the source explicitly supports transient crossed/locked states and the implementation documents them.

Initial implementation may use simple standard-library structures.

Potential implementations:

```text
std::map
flat/vector representation
fixed price-level arrays
custom compact structures
```

Any replacement motivated by performance must preserve behavior and pass the same tests.

---

# 12. Feature Engine

The feature engine consumes normalized market events and book state.

It must be separate from order submission.

Example features:

```text
mid-price
spread
best-level imbalance
multi-level imbalance
microprice
short-window return
short-window volatility
trade imbalance
order-flow imbalance
depth
```

Example best-level imbalance:

```text
I = (V_bid - V_ask) / (V_bid + V_ask)
```

Example microprice:

```text
P_micro =
(P_ask * V_bid + P_bid * V_ask)
/
(V_ask + V_bid)
```

Guard all divisions against zero denominator.

Feature calculation used in production must be deterministic for identical input event streams.

---

# 13. Strategy Interface

Strategies receive normalized state and emit **intent**, not broker-specific orders.

Conceptual output:

```cpp
struct OrderIntent {
    InstrumentId instrument;
    Side side;
    Price price;
    Quantity quantity;
    OrderType type;
};
```

Flow:

```text
Market Event
    |
Feature Engine
    |
Strategy
    |
Order Intent
    |
Risk Engine
    |
OMS
```

Strategies must not:

- call broker APIs directly
- mutate portfolio state directly
- bypass risk checks
- persist directly to disk
- own external network connections

---

# 14. Matching Engine

The exchange simulator must support:

- Limit buy
- Limit sell
- Market buy
- Market sell
- Cancel
- Modify or cancel/replace
- Partial fill
- Full fill
- Price-time priority

At each price level, orders must preserve FIFO priority.

Example:

```text
Price 101

A: qty 100, entered t1
B: qty 150, entered t2
C: qty  50, entered t3
```

Incoming contra order quantity 180:

```text
A fills 100
B fills 80
C fills 0
```

Remaining queue:

```text
B: 70
C: 50
```

Matching-engine behavior must be covered by deterministic unit tests.

---

# 15. Latency-Aware Simulation

The simulator must not assume instantaneous execution.

Execution simulation should support configurable delays such as:

```text
strategy processing latency
outbound network latency
exchange processing latency
inbound acknowledgement latency
```

Conceptual flow:

```text
signal generated at t0
        |
risk / OMS
        |
outbound latency
        |
order reaches simulated exchange at t1
        |
process all intervening market events
        |
match / queue / reject
        |
return acknowledgement or fill
```

The simulator must be able to compare the same strategy under different latency assumptions.

---

# 16. Order Management System

Order submission and execution are asynchronous.

The OMS must use explicit order states.

Recommended states:

```cpp
enum class OrderState {
    LocalCreated,
    PendingSubmit,
    Submitted,
    Acknowledged,
    Live,
    PartiallyFilled,
    CancelPending,
    Cancelled,
    Filled,
    Rejected,
    Unknown
};
```

Track at minimum:

```text
client_order_id
broker_order_id if known
instrument
side
type
price
original_quantity
filled_quantity
remaining_quantity
state
creation_timestamp
last_update_timestamp
```

Required invariant:

```text
filled_quantity <= original_quantity
```

and:

```text
remaining_quantity =
original_quantity - filled_quantity
```

unless a provider exposes a documented behavior requiring additional handling.

---

# 17. Client and Broker Order IDs

Internal order identity and broker identity must remain separate.

Example:

```text
ClientOrderId = 92847
BrokerOrderId = 116737821
```

The OMS owns the client ID.

The adapter records the broker ID once acknowledged.

Maintain a mapping:

```text
ClientOrderId <-> BrokerOrderId
```

Do not reuse client order IDs during one persistent trading history unless explicitly designed and documented.

---

# 18. Duplicate and Out-of-Order Broker Events

Broker callbacks may be:

- delayed
- duplicated
- reordered
- incomplete across disconnects

Handlers must be idempotent where practical.

A duplicate fill must never change the position twice.

Use stable execution/fill identifiers when supplied by the broker.

If order state cannot be reconstructed safely, transition to:

```text
Unknown
```

or a degraded runtime state and reconcile with the broker before enabling new orders.

---

# 19. Portfolio and Accounting

Maintain local portfolio state.

Per account:

```text
cash
positions
realized_pnl
unrealized_pnl
gross_exposure
net_exposure
outstanding_order_exposure
```

Per instrument:

```text
quantity
average_entry_price
market_value
realized_pnl
unrealized_pnl
pending_buy_quantity
pending_sell_quantity
```

Portfolio changes must be caused by execution/account events, not strategy intent.

Submitting an order must not immediately change the filled position.

---

# 20. Pending Exposure

Risk calculations must include outstanding orders.

Example:

```text
current position: +70
pending BUY:      +50
pending SELL:     -20
```

Worst-case long exposure:

```text
+120
```

Worst-case short-side reduction/exposure must be calculated consistently with configured policy.

Risk logic must not look only at current filled position.

---

# 21. PnL

Support at least:

```text
realized PnL
unrealized PnL
total PnL
```

Document the cost-basis convention.

Do not mix:

- exchange/broker reported PnL
- internally calculated PnL

without labeling the source.

Fees and commissions must be represented separately and included in net PnL when available.

---

# 22. Risk Engine

All strategy-generated order intents must pass through pre-trade risk.

No strategy may bypass the risk engine.

Potential checks:

```text
max_order_quantity
max_position
max_gross_exposure
max_net_exposure
max_pending_exposure
max_orders_per_second
max_daily_loss
market_data_freshness
market_data_validity
broker_connection_state
trading_session_state
instrument_enabled
system_state
```

Example:

```text
if market_data_state != VALID:
    reject

if market_data_age > configured_timeout:
    reject

if projected_position > max_position:
    reject

if daily_net_pnl < -daily_loss_limit:
    disable trading
```

Risk rejection must be observable and persisted where appropriate.

---

# 23. Kill Switch

Provide an explicit kill-switch path.

A kill switch must be able to:

```text
disable new strategy-generated orders
request cancellation of live orders
mark runtime as non-trading
emit an observable state change
```

The implementation must define what happens if cancellation fails or the broker is disconnected.

A kill switch must not depend on the strategy behaving correctly.

---

# 24. Broker Reconciliation

The local system maintains low-latency state, but actual broker-side account state must be periodically reconciled.

Reconcile at least:

```text
positions
open orders
executions
cash
account balances
```

Example:

```text
LOCAL:  AAPL +5
BROKER: AAPL +5
=> consistent
```

Mismatch:

```text
LOCAL:  AAPL +5
BROKER: AAPL +4
=> reconciliation failure
```

On material mismatch:

```text
disable new trading
enter degraded/reconciliation state
record discrepancy
resolve from broker/execution history
```

Do not silently overwrite unknown discrepancies and continue trading.

---

# 25. Runtime State Machine

Use an explicit system-level state machine.

Recommended states:

```cpp
enum class SystemState {
    Starting,
    Connecting,
    Reconciling,
    SynchronizingMarketData,
    Ready,
    Trading,
    Degraded,
    Stopping,
    Stopped
};
```

Only permitted states may send new strategy orders.

Typical startup flow:

```text
STARTING
   |
load configuration
   |
initialize persistence
   |
load recoverable local state
   |
connect broker
   |
retrieve broker positions/orders/executions
   |
RECONCILE
   |
connect market data
   |
subscribe
   |
synchronize books
   |
verify freshness
   |
READY
   |
TRADING
```

No trading before reconciliation and valid market-data synchronization.

---

# 26. Shutdown

Support graceful termination.

On SIGTERM/SIGINT:

```text
stop strategy order generation
        |
transition to Stopping
        |
handle/cancel outstanding orders according to policy
        |
flush critical persistence
        |
close broker/data connections
        |
stop worker threads
        |
transition to Stopped
```

Shutdown must have a timeout policy so it cannot hang forever.

Do not terminate a persistence worker before critical state transitions are written.

---

# 27. Persistence

Persist important state-changing events.

At minimum support an append-only journal.

Potential event types:

```text
SystemStateChanged
ConnectionStateChanged
MarketDataStateChanged
SignalGenerated
RiskAccepted
RiskRejected
OrderSubmitted
OrderAcknowledged
OrderRejected
OrderCancelled
FillReceived
PortfolioUpdated
ReconciliationResult
```

Suggested envelope:

```text
sequence
timestamp
event_type
version
payload
```

The journal format must be versionable.

Corrupted/incomplete tail records after a crash must be detectable.

---

# 28. Recovery

On restart:

```text
load persisted local state/journal
        |
reconstruct recoverable state
        |
connect broker
        |
fetch broker truth
        |
reconcile
        |
connect market data
        |
resynchronize
        |
enable trading only if consistent
```

Do not trust persisted portfolio/order state without broker reconciliation.

Do not reset positions to zero simply because the process restarted.

---

# 29. Hot Path and Cold Path

Treat these as different concerns.

## Hot path

Examples:

```text
market-data parse
normalization
book update
feature update
strategy evaluation
risk check
order creation
broker send
```

Avoid where practical:

- blocking disk I/O
- synchronous database calls
- slow text formatting
- blocking network logging
- unnecessary heap allocation
- long mutex-held sections

## Cold path

Examples:

```text
metrics aggregation
structured logging
journal writing
reporting
reconciliation
configuration
administrative commands
```

Cold-path work should not block the critical trading path unless required for correctness.

---

# 30. Concurrency

Start from a deterministic single-threaded event loop unless requirements prove insufficient.

When concurrency is introduced:

- document thread ownership
- document shared state
- minimize shared mutable data
- prefer single-writer ownership
- use queues between subsystems where appropriate
- avoid lock-free structures unless justified by measurements

Possible architecture:

```text
Market Data Thread
       |
    SPSC Queue
       |
 Trading Thread
       |
    SPSC Queue
       |
 Broker / I/O Thread
```

This is an option, not a requirement.

Multi-threaded code must preserve deterministic unit-testable components.

---

# 31. Networking

External connectivity may use:

```text
TCP
WebSocket
HTTP/REST
provider-specific socket APIs
```

The adapter must handle:

- connection establishment
- authentication
- reconnect
- heartbeat/keepalive where applicable
- timeout
- partial messages
- protocol errors
- subscription recovery
- backpressure where relevant

Networking errors must be translated into internal connection-state events.

---

# 32. Broker Integration

The primary broker integration may target Interactive Brokers through a dedicated adapter.

Provider-specific code must remain isolated under:

```text
cpp/broker/<provider>/
```

The core OMS must not depend on the provider SDK.

The adapter is responsible for:

```text
provider authentication
connection lifecycle
order submission
order cancellation
provider order IDs
order acknowledgements
fills
rejections
positions
open-order retrieval
account retrieval
execution retrieval
```

Paper trading must be the default external execution environment.

---

# 33. Market Data Providers

Provider-specific feed adapters belong under:

```text
cpp/market_data/<provider>/
```

Adapters should convert:

```text
L1
L2
L3 if later supported
trades
quotes
provider resets
sequence numbers
```

into normalized internal events.

The core book must not parse JSON, provider SDK classes, WebSocket frames, or vendor binary records directly.

---

# 34. Configuration

Runtime behavior must be externally configurable.

Example configuration keys:

```text
environment = paper
broker = ...
market_data_provider = ...
instrument = ...
strategy = ...
max_position = ...
max_order_size = ...
daily_loss_limit = ...
market_data_timeout_ms = ...
trading_enabled = false
```

Use typed configuration parsing.

Validate configuration at startup.

Invalid risk or connectivity configuration must fail closed.

---

# 35. Secrets

Never commit:

```text
API keys
passwords
session tokens
private certificates
production account identifiers if sensitive
```

Use:

- environment variables
- local secret files excluded by `.gitignore`
- operating-system secret facilities
- deployment secret management

Do not print secrets in logs.

---

# 36. Monitoring

Expose enough state to determine whether the runtime is healthy.

Recommended metrics:

```text
process_uptime
system_state
broker_connected
market_data_connected
market_data_state
market_data_age_ms

market_messages_total
market_messages_per_second

orders_sent_total
orders_rejected_total
orders_live

fills_total

current_position
gross_exposure
net_exposure
daily_pnl

strategy_latency_ns_p50
strategy_latency_ns_p95
strategy_latency_ns_p99

order_book_update_latency_ns_p50
order_book_update_latency_ns_p99

event_queue_depth
persistence_queue_depth
error_count
```

A simple structured-log implementation is acceptable before introducing external monitoring systems.

---

# 37. Structured Logging

Logs should be machine-readable where practical.

Include fields such as:

```text
timestamp
severity
component
event
instrument
client_order_id
broker_order_id
system_state
message
```

Avoid constructing expensive debug strings in the hot path when debug logging is disabled.

Do not rely on logs as the only durable source of order/fill state.

---

# 38. Failure Handling

Expected behavior must be defined for at least:

| Failure | Required behavior |
|---|---|
| Market-data disconnect | Disable new trading |
| Market-data stale | Disable new trading |
| Sequence gap | Invalidate/resynchronize book |
| Broker disconnect | Enter degraded state |
| Order reject | Update OMS and portfolio exposure |
| Partial fill | Apply only filled quantity |
| Duplicate fill | Ignore duplicate state change |
| Unknown order state | Reconcile before new trading |
| Process crash | Recover and reconcile |
| Position mismatch | Disable trading |
| Unknown broker order | Reconcile/investigate |
| Risk limit breach | Reject or kill according to policy |
| Daily loss breach | Trigger kill switch |
| Persistence error | Fail according to configured safety policy |
| SIGTERM/SIGINT | Graceful shutdown |

Failure behavior must be covered by integration or failure-injection tests.

---

# 39. Testing

## Unit Tests

Cover:

```text
price conversion
event parsing
book updates
best bid/ask
feature calculations
matching
order state transitions
portfolio accounting
PnL
risk checks
reconciliation logic
runtime state transitions
```

---

## Invariant Tests

Examples:

```text
filled_quantity <= original_quantity
remaining_quantity ==
    original_quantity - filled_quantity
```

For a valid ordinary book:

```text
best_bid < best_ask
```

A duplicate fill must not alter position or PnL twice.

A rejected order must not become a filled position.

---

## Integration Tests

Cover:

```text
historical replay -> book
book -> features -> strategy
strategy -> risk -> OMS
OMS -> matching engine
matching engine -> fills -> portfolio
broker adapter -> OMS callbacks
persistence -> restart -> recovery
broker reconciliation
market-data reconnect/resync
graceful shutdown
```

---

## Failure Injection Tests

Inject:

```text
disconnects
timeouts
sequence gaps
duplicate messages
out-of-order messages
rejections
partial fills
persistence failures
restart during live order
stale market data
position mismatch
```

All such tests must have deterministic expected outcomes.

---

# 40. Benchmarking

Benchmark both isolated components and end-to-end paths.

Candidate benchmarks:

```text
market-data normalization
OrderBook::update()
FeatureEngine::update()
Strategy::on_event()
RiskEngine::check()
OMS transition
matching engine
replay throughput
end-to-end market-event-to-order path
```

Report latency distributions, not only averages.

At minimum:

```text
median
p95
p99
operations/events per second
```

Where relevant also record:

```text
heap allocations
CPU utilization
queue depth
cache misses
branch misses
```

Never place unmeasured performance numbers in README or documentation.

---

# 41. Performance Changes

For every significant optimization:

1. Record baseline.
2. Run correctness tests.
3. Profile the relevant workload.
4. Identify a specific bottleneck.
5. Make one focused change.
6. Re-run tests.
7. Re-run benchmark.
8. Compare distributions.
9. Document trade-offs if architecture changed.

Do not replace clear standard-library code with custom low-level code without measured benefit.

---

# 42. Allocation Policy

Avoid unnecessary dynamic allocation in repeated hot-path operations.

Prefer:

```text
preallocated buffers
stable-capacity containers
object reuse
fixed-size message structures where practical
```

Do not introduce custom allocators until profiling identifies allocation as a real bottleneck.

Correctness and lifetime safety remain mandatory.

---

# 43. Build System

Use CMake.

Expected top-level flow:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Prefer target-scoped CMake configuration.

Use:

```cmake
target_include_directories(...)
target_compile_features(...)
target_link_libraries(...)
```

instead of global flags where possible.

Keep warnings enabled in development builds.

Treat sanitizer builds as separate configurations where practical.

---

# 44. Recommended Build Variants

Support or make it straightforward to use:

```text
Debug
Release
RelWithDebInfo
ASan/UBSan development build
```

Benchmark only optimized builds unless explicitly measuring debug behavior.

Do not compare benchmark results from materially different compiler settings without documenting them.

---

# 45. Deployment

Target autonomous Linux execution.

Use a native service model before adding containerization as a requirement.

Example layout:

```text
/etc/low-latency-trading-system/
    config.toml

/opt/low-latency-trading-system/
    trading_engine

/var/lib/low-latency-trading-system/
    journal/

/var/log/low-latency-trading-system/
    runtime.log
```

A `systemd` service should eventually manage:

```text
startup
restart policy
working directory
environment
signal forwarding
logs
shutdown
```

The deployment must default to paper mode and trading disabled unless explicitly enabled.

---

# 46. Autonomous Runtime Requirements

An unattended runtime must be able to:

```text
start
load configuration
recover persisted state
connect broker
fetch broker state
reconcile
connect market data
synchronize books
validate feed freshness
enter Ready
enable Trading only when permitted
process market events
submit/manage orders
update portfolio
persist state
monitor health
handle reconnects
shutdown cleanly
restart and reconcile
```

No human interaction should be required during a normal paper-trading session.

---

# 47. Safety Defaults

Defaults must be conservative.

Recommended defaults:

```text
environment = paper
trading_enabled = false
max_position = small
max_order_size = small
daily_loss_limit = small
cancel_on_shutdown = true
require_valid_market_data = true
require_broker_reconciliation = true
```

Live trading must require an explicit configuration change.

Do not make a code change that silently switches default execution from simulated/paper to live.

---

# 48. Out of Scope Unless Explicitly Requested

Do not introduce these technologies merely because they are associated with HFT:

```text
FPGA
CUDA
DPDK
RDMA
kernel bypass
exchange colocation
direct exchange membership
custom NIC programming
Kafka
microservices
distributed databases
reinforcement learning
transformer-based prediction
custom lock-free MPMC queues
NUMA-specific optimization
```

They may be added later only when the existing architecture provides a clear requirement.

---

# 49. Coding-Agent Rules

Before implementing a substantial change:

1. Identify the subsystem being modified.
2. Identify its state invariants.
3. Inspect existing interfaces before creating new abstractions.
4. Keep external-provider code inside adapters.
5. Avoid unrelated refactoring.
6. Add or update tests.
7. Add benchmarks for performance-sensitive changes.
8. Preserve deterministic replay behavior.
9. Document non-obvious failure handling.
10. Never add secrets to source control.

For broker/API changes:

1. Convert provider events into internal events at the boundary.
2. Treat provider callbacks as asynchronous.
3. Handle duplicate/delayed events.
4. Preserve client/broker ID mapping.
5. Keep reconciliation possible.
6. Fail closed if order/account state becomes ambiguous.

For runtime/deployment changes:

1. Never enable trading before reconciliation.
2. Never trade using invalid or stale market data.
3. Ensure shutdown cannot leave local state falsely marked as reconciled.
4. Ensure restart requires broker reconciliation.
5. Preserve paper-mode defaults.

For performance changes:

1. Benchmark first.
2. Profile.
3. Optimize one identified bottleneck.
4. Re-run correctness tests.
5. Re-run benchmarks.
6. Document the measured result.

---

# 50. Implementation Completion Criteria

A component is not complete merely because it compiles.

For a new component, require as applicable:

```text
defined interface
documented invariants
unit tests
integration coverage
failure behavior
observability
configuration
persistence behavior
benchmark if latency-sensitive
```

The integrated runtime is considered implementation-complete only when the same core engine can:

```text
replay historical events
run against the exchange simulator
connect to a paper broker
reconcile broker state
operate autonomously
recover after restart
enforce risk limits
persist critical state
report health
shut down safely
```
