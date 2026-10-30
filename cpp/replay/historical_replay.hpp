#pragma once

#include "market_data/normalized_csv_reader.hpp"
#include "runtime/order_book_market_data_handler.hpp"

namespace trading {
class ReplayError : public std::runtime_error {
public:
    ReplayError(const std::string& phase, std::size_t line, BookError error);
    [[nodiscard]] BookError book_error() const noexcept { return error_; }
private:
    BookError error_;
};

struct ReplaySummary {
    InstrumentId instrument;
    std::uint64_t snapshot_events;
    std::uint64_t update_events;
    bool operator==(const ReplaySummary&) const = default;
};

// Synchronous, as-fast-as-possible replay. Each run starts a new session.
// Only a successful return supplies a usable result. On book errors the book
// remains Invalid; other errors clear partial state to Empty (or no book).
class HistoricalReplay {
public:
    [[nodiscard]] ReplaySummary run(std::istream& snapshot, std::istream& updates,
                                    const DecimalTickSize& tick_size);
    [[nodiscard]] const std::optional<OrderBook>& book() const noexcept { return handler_.book(); }
private:
    OrderBookMarketDataHandler handler_;
};
} // namespace trading
