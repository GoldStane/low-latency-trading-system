#pragma once

#include "market_data/market_data_source.hpp"
#include "market_data/normalized_csv_reader.hpp"
#include <memory>

namespace trading {
// One-shot session; borrowed streams are never rewound, sorted, or deduplicated.
class NormalizedCsvMarketDataSource final : public MarketDataSource {
public:
    NormalizedCsvMarketDataSource(std::istream& snapshot, std::istream& updates,
        const DecimalTickSize& tick, MarketDataSink& sink);
    ~NormalizedCsvMarketDataSource() override;
    void start() override;
    bool poll() override;
    void stop() override;
    const char* phase() const noexcept { return snapshot_phase_ ? "snapshot" : "updates"; }
    std::size_t line() const noexcept { return reader_ ? reader_->line() : 1; }
    InstrumentId instrument() const noexcept { return instrument_; }
    std::uint64_t snapshot_events() const noexcept { return snapshot_events_; }
    std::uint64_t update_events() const noexcept { return update_events_; }
private:
    void fail() noexcept;
    std::istream& snapshot_;
    std::istream& updates_;
    DecimalTickSize tick_;
    std::unique_ptr<NormalizedCsvReader> reader_;
    bool started_{false}, snapshot_phase_{true};
    InstrumentId instrument_{};
    std::uint64_t snapshot_events_{}, update_events_{};
};
} // namespace trading
