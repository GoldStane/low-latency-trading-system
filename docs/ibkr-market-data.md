# Experimental IBKR market data

This is a data-only, one-stock connectivity experiment. It does not submit,
modify, or cancel orders. It does not reconstruct IBKR depth, authorize trading,
reconcile accounts, or run an unattended daemon. `ENABLE_IBKR` is OFF by default.

## SDK and build

The adapter targets **TWS API 10.50.02**, whose actual C++ headers and external
library were used for build verification. Do not assume compatibility with older
SDK callback signatures. Obtain the external SDK from the
[official download page](https://download2.interactivebrokers.com/installers/tws-api/index.html).
The configured root is the directory containing `source/cppclient/client`, often
named `IBJts`; the installer may let you choose another name. No SDK code or binary
is vendored, downloaded by CMake, or required by normal tests.

Linux and macOS are supported by this experimental session. Dependencies are a
C++20 compiler, CMake, threads, the Intel decimal library, and Protobuf compatible
with the SDK's generated headers. The 10.50.02 `protobufUnix` headers were generated
with Protobuf **3.12.4**; use that version, or regenerate all SDK Protobuf sources
consistently for your installed version. Merely installing a newer Protobuf library
may produce incompatible generated-code errors.

Build Intel's decimal library for the same CPU/toolchain, following the SDK's
`source/cppclient/Intel_lib_build.txt` and
[IBKR's decimal dependency instructions](https://www.interactivebrokers.com/docs/tws-api/doc/download-the-tws-api/unique-configurations/implementing-the-intel-decimal-library-for-mac-os-and-linux).
The SDK expects CALL_BY_REF=0, GLOBAL_RND=0, GLOBAL_FLAGS=0,
UNCHANGED_BINARY_FLAGS=0. An x86 binary cannot be linked into an ARM build.

A small standalone CMake helper compiles SDK sources in their external location.
For example, after installing compatible Protobuf and building `libbid.a`:

```sh
cmake -S cmake/ibkr-sdk -B build-ibkr-sdk -DCMAKE_BUILD_TYPE=Release \
    -DIBKR_API_ROOT=/path/to/IBJts \
    -DIBKR_DECIMAL_LIBRARY=/path/to/IntelRDFPMathLib20U5/LIBRARY/libbid.a \
    -DCMAKE_PREFIX_PATH=/path/to/protobuf-install
cmake --build build-ibkr-sdk

cmake -S . -B build-ibkr -DCMAKE_BUILD_TYPE=Debug -DENABLE_IBKR=ON \
    -DIBKR_API_ROOT=/path/to/IBJts \
    -DIBKR_API_LIBRARY="$PWD/build-ibkr-sdk/libtwsapi.a" \
    -DIBKR_EXTRA_LIBRARIES=/path/to/IntelRDFPMathLib20U5/LIBRARY/libbid.a \
    -DCMAKE_PREFIX_PATH=/path/to/protobuf-install
cmake --build build-ibkr
ctest --test-dir build-ibkr --output-on-failure
```

Alternatively provide your independently built SDK library. `IBKR_EXTRA_LIBRARIES`
is a semicolon-separated list of additional link dependencies for static SDKs.
Use matching SDK headers and library. Normal `cmake -S . -B build` needs none of
these dependencies. Enabling IBKR with a missing SDK or missing compiled library
fails during configuration with an actionable message.

## TWS / IB Gateway setup

Run TWS (interactive desktop) or IB Gateway (a smaller API-focused application),
and log into the **paper** session. Enable socket/API clients in API settings,
confirm the configured socket port, and allow the local client connection. Keep
**Read-Only API** enabled: this experiment only needs market data. Use a unique
client ID. The program does not store or accept login credentials.

Default host is `127.0.0.1`, port `7497` (paper TWS), client ID `7`, exchange `SMART`,
currency `USD`, instrument ID `1`, tick size `0.01`, market-data mode `3`, and
startup timeout `15` seconds. Paper IB Gateway commonly uses `4002`; verify the
actual setting in your application. Symbol is mandatory. `--primary-exchange`
disambiguates listings. Security type is intentionally limited to stocks (`STK`).

```sh
./build-ibkr/ibkr_market_data --symbol AAPL --primary-exchange NASDAQ \
    --host 127.0.0.1 --port 7497 --client-id 7 \
    --exchange SMART --currency USD --instrument-id 1 \
    --tick-size 0.01 --market-data-mode 3 --startup-timeout-seconds 15
```

Modes are 1 realtime, 2 frozen, 3 delayed, 4 delayed-frozen. This selects the
requested data mode, not the account's paper/live environment. IBKR may return
realtime data when permissions permit; output labels the confirmed callback mode.
Paper accounts still depend on subscriptions, exchange permissions, competing
sessions, and market hours. Delayed data is not universally available. A successful
connection does not guarantee data entitlement. See
[IBKR streaming data](https://www.interactivebrokers.com/docs/tws-api/doc/md/streaming-data)
and [market-data types](https://interactivebrokers.github.io/tws-api/market_data_type.html).

Output reports connection changes and normalized quote fields. Prices are integer
ticks, missing values print `none`, and exchange time explicitly prints `none`.
`mode=0` means unknown, while 1–4 have the meanings above. Quote output may be
partial while fields arrive. This is diagnostic console output, not latency data.

Ctrl+C/SIGINT or SIGTERM sets only a signal-safe flag. The main loop cancels the
market-data subscription with `cancelMktData`, shuts down the socket to wake a
reader blocked on a partial message, disconnects, and joins the SDK reader before
destroying its dependencies. `cancelMktData` cancels a data subscription, not an
order. Destruction also releases resources if parsing/dispatch/API work throws.

## API boundary and lifecycle

The session uses `eConnect`, API-ready `nextValidId` (the value is ignored),
`reqContractDetails`/`contractDetails`/`contractDetailsEnd`, `reqMarketDataType`,
`reqMktData`, `marketDataType`, `tickPrice`, `tickSize`, `cancelMktData`, and
`eDisconnect`. It requires exactly one contract match before subscribing. Only
market-data requests are sent. The SDK's default wrapper ignores unrelated API
callbacks; no order, fill, position, or account handler is implemented here.

The SDK reader thread reads its socket queue. `poll()` processes callbacks on the
owner thread; quote translation and sink calls occur there. Socket error callbacks
can originate on the reader thread and only set an atomic failure indicator plus
a guarded diagnostic. Polling surfaces the failure and closes the source. No core
consumer is called concurrently.

Only informational farm notices 2104/2106/2107/2108/2158 are ignored. Other API
errors, ambiguous contracts, failed normalization, and disconnects fail closed.
Data-loss/restoration notifications also require a new explicit session; there is
no automatic reconnect or reuse of old quote fields. The initial handshake,
contract resolution, and first bid/ask callback share a monotonic startup deadline.
The SDK's synchronous `eConnect` itself is subject to OS connection/handshake
behavior; the deadline is checked in polling after it returns. A fully bounded
nonblocking connect and ongoing freshness monitoring are deferred.

## Semantics and conversion

Watchlist data is not a sequenced, absolute-level depth feed. It cannot safely
be converted to the existing ADD/MODIFY/DELETE contract. The adapter emits the
provider-neutral `Quote` variant through the same sink boundary as historical
BookUpdate. The book handler ignores Quote; a quote printer consumes it. No feed
sequence, snapshot completeness, or exchange timestamp is fabricated.

Prices and sizes arrive separately. Every price callback replaces that side's
price and clears its old quantity until a size callback arrives. Each side keeps
independent price/quantity receive times, so cached opposite-side fields cannot
be mistaken for an atomic snapshot. Missing price (-1 or zero for this supported
positive-price stock scope) and missing size (-1) are explicit optionals. Size zero
is retained as zero. Duplicates are observable observations, not book mutations.
Crossed or locked observations are preserved without declaring a valid book.

Receive timestamps are local Unix epoch nanoseconds captured at callback dispatch,
not socket-arrival timestamps or latency measurements. Exchange time is absent.
The translator never uses a wall clock for benchmarking or deadline checks.

The configured tick size is instrument metadata supplied by the caller. Price is
divided by that increment in long-double arithmetic, rounded to the nearest tick,
and accepted only within **1e-6 ticks** of the integer. Nonfinite/nonpositive prices,
off-grid values, and values beyond 2^53−1 ticks are rejected. The tolerance is fixed,
not price-relative, so large prices do not acquire half-tick rounding permission.
The bound is deliberately stricter than int64 to avoid pretending binary64 inputs
carry more integer precision than they do. This conversion does not replace the
exact decimal-string historical parser. Variable tick schedules are not inferred.

SDK Decimal quantities are converted to decimal display text at the SDK boundary,
then validated exactly as whole uint32 units. Fractional, overflowing, nonfinite,
and malformed sizes fail rather than truncate. SDK Decimal objects do not reach
core events. The offline SDK test checks actual Decimal string conversion.

## Verification and remaining acceptance check

Validated locally with AppleClang 21, macOS ARM64, IBKR API 10.50.02, Protobuf 3.12.4,
and Intel Decimal 2.0 Update 5. Normal Debug/Release and ASan/UBSan CTest suites
pass; the optional SDK target builds and has additional SDK/CLI tests without
opening a connection. A manual attempt on the default paper port exits cleanly
with Failed when no listener is present. Missing-SDK configuration also fails as
intended.

**Actual authenticated TWS/IB Gateway quote receipt remains pending**, explicitly
accepted as pending for this implementation session. Run the command above against
your paper session, confirm Connected and populated bid/ask observations, then
verify Ctrl+C termination. Offline checks and successful linking do not prove this
live acceptance criterion. No real or paper orders are ever sent by this tool.

L2/depth reconstruction, reconnect/resync, ongoing freshness gates, broader product
support, strategies, execution, risk, OMS, portfolio, persistence, and autonomous
deployment are intentionally deferred.
