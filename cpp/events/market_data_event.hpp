#pragma once

#include "events/book_update.hpp"
#include <variant>

namespace trading {
enum class ConnectionState : std::uint8_t { Disconnected, Connecting, Connected, Completed, Failed };
struct ConnectionStateChanged { ConnectionState state; };
struct SnapshotBegin { InstrumentId instrument; };
struct SnapshotEnd {};
enum class QuoteMode : std::uint8_t { Unknown, Realtime, Frozen, Delayed, DelayedFrozen };
// Latest observed fields, not an atomic exchange snapshot or a depth update.
// A new price invalidates its previous size until a size callback arrives.
struct QuoteSide {
    std::optional<Price> price;
    std::optional<Quantity> quantity;
    std::optional<Timestamp> price_receive_time;
    std::optional<Timestamp> quantity_receive_time;
};
struct Quote {
    Timestamp receive_time;
    std::optional<Timestamp> exchange_time;
    InstrumentId instrument;
    QuoteSide bid;
    QuoteSide ask;
    QuoteMode mode;
};
using MarketDataEvent = std::variant<BookUpdate, SnapshotBegin, SnapshotEnd, Quote, ConnectionStateChanged>;
} // namespace trading
