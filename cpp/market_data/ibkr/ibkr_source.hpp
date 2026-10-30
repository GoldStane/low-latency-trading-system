#pragma once
#include "market_data/market_data_source.hpp"
#include <memory>
#include <string>

namespace trading::ibkr {
struct Config {
    std::string host{"127.0.0.1"};
    int port{7497};
    int client_id{7};
    std::string symbol;
    std::string exchange{"SMART"};
    std::string primary_exchange;
    std::string currency{"USD"};
    InstrumentId instrument{1};
    double tick_size{0.01};
    int market_data_mode{3};
    int startup_timeout_seconds{15};
};
// SDK types never leave the private implementation. One reader thread belongs
// to the SDK; callbacks and all normalized event delivery run on the poll owner.
class IbkrMarketDataSource final : public MarketDataSource {
public:
    IbkrMarketDataSource(Config config, MarketDataSink& sink);
    ~IbkrMarketDataSource() override;
    void start() override;
    bool poll() override;
    void stop() override;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trading::ibkr
