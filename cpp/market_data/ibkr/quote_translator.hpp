#pragma once

#include "market_data/market_data_source.hpp"
#include <string_view>

namespace trading::ibkr {
// No SDK dependency: callback numbers and decimal text stop at this boundary.
class QuoteTranslator {
public:
    QuoteTranslator(InstrumentId instrument, double tick_size, MarketDataSink& sink);
    void price(int field, double value, Timestamp received);
    void size(int field, std::string_view value, Timestamp received);
    void mode(int value, Timestamp received);
    void reset() noexcept;
    Price to_ticks(double value) const;
private:
    void emit(Timestamp received);
    QuoteSide* side(int field, bool price_field);
    double tick_;
    MarketDataSink& sink_;
    Quote quote_;
};
} // namespace trading::ibkr
