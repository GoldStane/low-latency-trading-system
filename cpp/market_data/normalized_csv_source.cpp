#include "market_data/normalized_csv_source.hpp"
#include <limits>

namespace trading {
NormalizedCsvMarketDataSource::NormalizedCsvMarketDataSource(std::istream& snapshot, std::istream& updates,
    const DecimalTickSize& tick, MarketDataSink& sink)
    : MarketDataSource(sink), snapshot_(snapshot), updates_(updates), tick_(tick) {}

NormalizedCsvMarketDataSource::~NormalizedCsvMarketDataSource() {
    try { stop(); } catch (...) {} // Borrowed sink outlives source; destructors cannot propagate.
}
void NormalizedCsvMarketDataSource::fail() noexcept {
    // Preserve the original parser/consumer exception if notification also fails.
    try { transition(ConnectionState::Failed); } catch (...) {}
}
void NormalizedCsvMarketDataSource::start() {
    if (started_) throw std::logic_error("CSV source is one-shot; construct a fresh session");
    started_ = true;
    try {
        transition(ConnectionState::Connecting);
        reader_ = std::make_unique<NormalizedCsvReader>(snapshot_, tick_);
        transition(ConnectionState::Connected);
    } catch (...) { fail(); throw; }
}
bool NormalizedCsvMarketDataSource::poll() {
    if (state() == ConnectionState::Completed) return false;
    if (state() != ConnectionState::Connected) throw std::logic_error("CSV source is not connected");
    try {
        // Create the incremental reader only after successful snapshot completion.
        if (!reader_) reader_ = std::make_unique<NormalizedCsvReader>(updates_, tick_);
        const auto update = reader_->next();
        if (update) {
            if (snapshot_phase_ && snapshot_events_ == 0) {
                instrument_ = update->instrument;
                sink_.on_event(SnapshotBegin{instrument_});
            }
            if (snapshot_phase_ && update->type != UpdateType::Add)
                throw CsvError(line(), "snapshot rows must use ADD");
            auto& count = snapshot_phase_ ? snapshot_events_ : update_events_;
            if (count == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("event count overflow");
            sink_.on_event(*update);
            ++count;
            return true;
        }
        if (snapshot_phase_) {
            sink_.on_event(SnapshotEnd{});
            snapshot_phase_ = false;
            reader_.reset();
            return true;
        }
        transition(ConnectionState::Completed);
        return false;
    } catch (...) { fail(); throw; }
}
void NormalizedCsvMarketDataSource::stop() {
    if (state() != ConnectionState::Completed) transition(ConnectionState::Disconnected);
}
} // namespace trading
