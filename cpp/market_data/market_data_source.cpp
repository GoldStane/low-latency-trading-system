#include "market_data/market_data_source.hpp"

namespace trading {
void MarketDataSource::transition(ConnectionState next) {
    if (next == state_) return;
    const bool legal =
        (state_ == ConnectionState::Disconnected && next == ConnectionState::Connecting) ||
        (state_ == ConnectionState::Connecting && next == ConnectionState::Connected) ||
        (state_ == ConnectionState::Connected && next == ConnectionState::Completed) ||
        ((state_ == ConnectionState::Connecting || state_ == ConnectionState::Connected ||
          state_ == ConnectionState::Completed) && next == ConnectionState::Failed) ||
        next == ConnectionState::Disconnected;
    if (!legal) throw std::logic_error("illegal market-data source transition");
    state_ = next;
    sink_.on_event(ConnectionStateChanged{next});
}
} // namespace trading
