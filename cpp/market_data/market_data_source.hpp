#pragma once

#include "events/market_data_event.hpp"
#include <stdexcept>

namespace trading {
class MarketDataSink {
public:
    virtual ~MarketDataSink() = default;
    virtual void on_event(const MarketDataEvent& event) = 0;
};

// Single-owner, synchronous delivery. Sink must outlive source. No reentrant
// calls from sinks. Exceptions stop delivery; callers must not resume failures.
class MarketDataSource {
public:
    virtual ~MarketDataSource() = default;
    virtual void start() = 0;
    // Bounded work per call; true means more work may arrive, not an event count.
    virtual bool poll() = 0;
    virtual void stop() = 0;
    [[nodiscard]] ConnectionState state() const noexcept { return state_; }
protected:
    explicit MarketDataSource(MarketDataSink& sink) : sink_(sink) {}
    void transition(ConnectionState next);
    MarketDataSink& sink_;
private:
    ConnectionState state_{ConnectionState::Disconnected};
};
} // namespace trading
