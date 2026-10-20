#pragma once

#include "events/book_update.hpp"

#include <cstddef>
#include <map>
#include <optional>

namespace trading {
struct PriceLevel {
    Price price;
    Quantity quantity;
    bool operator==(const PriceLevel&) const = default;
};

enum class BookError : std::uint8_t {
    None, NotSyncing, NotReady, WrongInstrument, InvalidEvent,
    SequenceGap, TimeRegression, DuplicateLevel, MissingLevel, CrossedBook,
    IncompleteBook, ResourceFailure
};

// Single owner, one instrument. No networking, clocks, or provider types.
class OrderBook {
public:
    explicit OrderBook(InstrumentId instrument) : instrument_(instrument) {}

    void clear() noexcept;
    void begin_sync() noexcept;
    [[nodiscard]] BookError finish_sync() noexcept;
    [[nodiscard]] BookError apply(const BookUpdate& update);

    [[nodiscard]] MarketDataState state() const noexcept { return state_; }
    [[nodiscard]] BookError last_error() const noexcept { return last_error_; }
    [[nodiscard]] std::optional<SequenceNumber> last_sequence() const noexcept { return sequence_; }
    [[nodiscard]] std::optional<PriceLevel> best_bid() const noexcept;
    [[nodiscard]] std::optional<PriceLevel> best_ask() const noexcept;
    [[nodiscard]] Quantity bid_volume(Price price) const noexcept;
    [[nodiscard]] Quantity ask_volume(Price price) const noexcept;
    [[nodiscard]] std::size_t bid_levels() const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_levels() const noexcept { return asks_.size(); }
    [[nodiscard]] bool invariants_hold() const noexcept;

private:
    using Levels = std::map<Price, Quantity>;
    BookError fail(BookError error) noexcept;
    bool crossed() const noexcept;

    InstrumentId instrument_;
    MarketDataState state_{MarketDataState::Empty};
    BookError last_error_{BookError::None};
    Levels bids_;
    Levels asks_;
    std::optional<SequenceNumber> sequence_;
    std::optional<Timestamp> exchange_time_;
};
} // namespace trading
