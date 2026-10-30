#include "market_data/ibkr/quote_translator.hpp"
#include <cmath>
#include <limits>

namespace trading::ibkr {
QuoteTranslator::QuoteTranslator(InstrumentId instrument, double tick_size, MarketDataSink& sink)
    : tick_(tick_size), sink_(sink), quote_{0, {}, instrument, {}, {}, QuoteMode::Unknown} {
    if (!std::isfinite(tick_) || tick_ <= 0) throw std::invalid_argument("invalid tick size");
}
Price QuoteTranslator::to_ticks(double value) const {
    const long double ticks = static_cast<long double>(value) / tick_;
    const long double rounded = std::round(ticks);
    // Limit to the exact-integer range of binary64 and reject off-grid prices.
    // Fixed tolerance in ticks never grows into accepting half-tick values.
    if (!std::isfinite(value) || value <= 0 || !std::isfinite(ticks) || rounded < 1 ||
        rounded > 9007199254740991.0L || std::abs(ticks - rounded) > 1e-6L)
        throw std::invalid_argument("IBKR price is out of range or not tick aligned (tolerance 1e-6 ticks)");
    return static_cast<Price>(rounded);
}
QuoteSide* QuoteTranslator::side(int field, bool price_field) {
    const bool delayed = price_field ? (field == 66 || field == 67) : (field == 69 || field == 70);
    const bool regular = price_field ? (field == 1 || field == 2) : (field == 0 || field == 3);
    if (!delayed && !regular) return nullptr;
    const bool delayed_mode = quote_.mode == QuoteMode::Delayed || quote_.mode == QuoteMode::DelayedFrozen;
    if (quote_.mode == QuoteMode::Unknown || delayed_mode != delayed)
        throw std::runtime_error("IBKR quote tick does not match confirmed market-data mode");
    return (price_field ? (field == 1 || field == 66) : (field == 0 || field == 69)) ? &quote_.bid : &quote_.ask;
}
void QuoteTranslator::emit(Timestamp received) {
    if (received < 0) throw std::invalid_argument("negative receive timestamp");
    quote_.receive_time = received;
    sink_.on_event(quote_);
}
void QuoteTranslator::price(int field, double value, Timestamp received) {
    auto* target = side(field, true);
    if (!target) return;
    if (received < 0) throw std::invalid_argument("negative receive timestamp");
    // -1 and zero mean unavailable for the supported positive-price stocks.
    const auto converted = value == -1 || value == 0 ? std::optional<Price>{} : std::optional<Price>{to_ticks(value)};
    target->price = converted;
    target->price_receive_time = received;
    target->quantity.reset();
    target->quantity_receive_time.reset();
    emit(received);
}
void QuoteTranslator::size(int field, std::string_view value, Timestamp received) {
    auto* target = side(field, false);
    if (!target) return;
    if (received < 0) throw std::invalid_argument("negative receive timestamp");
    std::optional<Quantity> quantity;
    if (value != "-1") {
        if (value.empty() || value.size() > 64) throw std::invalid_argument("invalid IBKR quantity");
        Quantity result = 0;
        bool point = false, digit = false, fraction_digit = false;
        for (char c : value) {
            if (c == '.' && !point && digit) { point = true; continue; }
            if (c < '0' || c > '9' || (point && c != '0'))
                throw std::invalid_argument("IBKR quantity must be whole units");
            digit = true;
            if (point) { fraction_digit = true; continue; }
            const auto d = static_cast<Quantity>(c - '0');
            if (result > (std::numeric_limits<Quantity>::max() - d) / 10)
                throw std::invalid_argument("IBKR quantity overflow");
            result = result * 10 + d;
        }
        if (point && !fraction_digit) throw std::invalid_argument("invalid IBKR quantity");
        quantity = result;
    }
    target->quantity = quantity;
    target->quantity_receive_time = received;
    emit(received);
}
void QuoteTranslator::mode(int value, Timestamp received) {
    if (value < 1 || value > 4 || received < 0) throw std::invalid_argument("invalid IBKR data mode/time");
    reset();
    quote_.mode = static_cast<QuoteMode>(value);
    emit(received);
}
void QuoteTranslator::reset() noexcept {
    quote_ = Quote{0, {}, quote_.instrument, {}, {}, QuoteMode::Unknown};
}
} // namespace trading::ibkr
