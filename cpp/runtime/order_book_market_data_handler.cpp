#include "runtime/order_book_market_data_handler.hpp"

namespace trading {
void OrderBookMarketDataHandler::on_event(const MarketDataEvent& event) {
    BookError error = BookError::None;
    if (const auto* begin = std::get_if<SnapshotBegin>(&event)) {
        book_.emplace(begin->instrument);
        book_->begin_sync();
    } else if (const auto* update = std::get_if<BookUpdate>(&event)) {
        if (!book_) throw BookDispatchError(BookError::NotReady);
        error = book_->apply(*update);
    } else if (std::holds_alternative<SnapshotEnd>(event)) {
        error = book_ ? book_->finish_sync() : BookError::IncompleteBook;
    } else if (const auto* connection = std::get_if<ConnectionStateChanged>(&event)) {
        if (connection->state == ConnectionState::Connecting) book_.reset();
        else if ((connection->state == ConnectionState::Failed || connection->state == ConnectionState::Disconnected)
                 && book_ && book_->state() != MarketDataState::Invalid) book_->clear();
    }
    // Quotes cannot establish absolute price-level book state; intentionally ignored.
    if (error != BookError::None) throw BookDispatchError(error);
}
} // namespace trading
