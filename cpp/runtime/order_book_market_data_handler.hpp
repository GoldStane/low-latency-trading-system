#pragma once

#include "market_data/market_data_source.hpp"
#include "order_book/order_book.hpp"

namespace trading {
class BookDispatchError : public std::runtime_error {
public:
    explicit BookDispatchError(BookError error) : std::runtime_error("book event rejected"), error_(error) {}
    BookError book_error() const noexcept { return error_; }
private:
    BookError error_;
};

class OrderBookMarketDataHandler final : public MarketDataSink {
public:
    void on_event(const MarketDataEvent& event) override;
    const std::optional<OrderBook>& book() const noexcept { return book_; }
private:
    std::optional<OrderBook> book_;
};
} // namespace trading
