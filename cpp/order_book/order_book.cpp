#include "order_book/order_book.hpp"

#include <limits>

namespace trading {
void OrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
    sequence_.reset();
    exchange_time_.reset();
    state_ = MarketDataState::Empty;
    last_error_ = BookError::None;
}

void OrderBook::begin_sync() noexcept {
    clear();
    state_ = MarketDataState::Syncing;
}

BookError OrderBook::fail(BookError error) noexcept {
    // Never expose stale levels after loss of consistency. Keep the last
    // accepted sequence/time for diagnostics until synchronization starts.
    bids_.clear();
    asks_.clear();
    state_ = MarketDataState::Invalid;
    last_error_ = error;
    return error;
}

bool OrderBook::crossed() const noexcept {
    return !bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first;
}

BookError OrderBook::finish_sync() noexcept {
    if (state_ != MarketDataState::Syncing) return fail(BookError::NotSyncing);
    if (bids_.empty() || asks_.empty()) return fail(BookError::IncompleteBook);
    if (crossed()) return fail(BookError::CrossedBook);
    state_ = MarketDataState::Valid;
    return BookError::None;
}

BookError OrderBook::apply(const BookUpdate& update) {
    if (state_ != MarketDataState::Syncing && state_ != MarketDataState::Valid)
        return fail(BookError::NotReady);
    if (update.instrument != instrument_) return fail(BookError::WrongInstrument);
    if ((update.side != Side::Bid && update.side != Side::Ask) ||
        (update.type != UpdateType::Add && update.type != UpdateType::Modify && update.type != UpdateType::Delete) ||
        update.price <= 0 || update.exchange_time < 0 ||
        (update.receive_time && *update.receive_time < 0) ||
        (update.type == UpdateType::Delete ? update.quantity != 0 : update.quantity == 0))
        return fail(BookError::InvalidEvent);
    if (sequence_ && (*sequence_ == std::numeric_limits<SequenceNumber>::max() ||
                      update.sequence != *sequence_ + 1))
        return fail(BookError::SequenceGap);
    if (exchange_time_ && update.exchange_time < *exchange_time_)
        return fail(BookError::TimeRegression);

    auto& levels = update.side == Side::Bid ? bids_ : asks_;
    const auto found = levels.find(update.price);
    if (update.type == UpdateType::Add) {
        if (found != levels.end()) return fail(BookError::DuplicateLevel);
        try {
            levels.emplace(update.price, update.quantity);
        } catch (...) {
            // Resource failure cannot leave a book marked usable.
            fail(BookError::ResourceFailure);
            throw;
        }
    } else {
        if (found == levels.end()) return fail(BookError::MissingLevel);
        if (update.type == UpdateType::Modify) found->second = update.quantity;
        else levels.erase(found);
    }
    if (state_ == MarketDataState::Valid && crossed()) return fail(BookError::CrossedBook);
    sequence_ = update.sequence;
    exchange_time_ = update.exchange_time;
    return BookError::None;
}

std::optional<PriceLevel> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) return std::nullopt;
    return PriceLevel{bids_.rbegin()->first, bids_.rbegin()->second};
}

std::optional<PriceLevel> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return std::nullopt;
    return PriceLevel{asks_.begin()->first, asks_.begin()->second};
}

Quantity OrderBook::bid_volume(Price price) const noexcept {
    const auto found = bids_.find(price);
    return found == bids_.end() ? 0 : found->second;
}

Quantity OrderBook::ask_volume(Price price) const noexcept {
    const auto found = asks_.find(price);
    return found == asks_.end() ? 0 : found->second;
}

bool OrderBook::invariants_hold() const noexcept {
    for (const auto& [price, quantity] : bids_) if (price <= 0 || quantity == 0) return false;
    for (const auto& [price, quantity] : asks_) if (price <= 0 || quantity == 0) return false;
    if ((state_ == MarketDataState::Empty || state_ == MarketDataState::Invalid) &&
        (!bids_.empty() || !asks_.empty())) return false;
    return state_ != MarketDataState::Valid || !crossed();
}
} // namespace trading
