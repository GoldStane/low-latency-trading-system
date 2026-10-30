#include "replay/historical_replay.hpp"

#include "market_data/normalized_csv_source.hpp"

namespace trading {
namespace {
const char* name(BookError error) {
    switch (error) {
    case BookError::None: return "None";
    case BookError::NotSyncing: return "NotSyncing";
    case BookError::NotReady: return "NotReady";
    case BookError::WrongInstrument: return "WrongInstrument";
    case BookError::InvalidEvent: return "InvalidEvent";
    case BookError::SequenceGap: return "SequenceGap";
    case BookError::TimeRegression: return "TimeRegression";
    case BookError::DuplicateLevel: return "DuplicateLevel";
    case BookError::MissingLevel: return "MissingLevel";
    case BookError::CrossedBook: return "CrossedBook";
    case BookError::IncompleteBook: return "IncompleteBook";
    case BookError::ResourceFailure: return "ResourceFailure";
    }
    return "UnknownBookError";
}

} // namespace

ReplayError::ReplayError(const std::string& phase, std::size_t line, BookError error)
    : std::runtime_error(phase + " line " + std::to_string(line) + ": " + name(error)), error_(error) {}

ReplaySummary HistoricalReplay::run(std::istream& snapshot, std::istream& updates,
                                     const DecimalTickSize& tick_size) {
    handler_ = OrderBookMarketDataHandler{};
    NormalizedCsvMarketDataSource source{snapshot, updates, tick_size, handler_};
    try {
        source.start();
        while (source.poll()) {}
        return {source.instrument(), source.snapshot_events(), source.update_events()};
    } catch (const BookDispatchError& error) {
        throw ReplayError(source.phase(), source.line(), error.book_error());
    } catch (const CsvError& error) {
        throw std::runtime_error(std::string(source.phase()) + " " + error.what());
    }
}
} // namespace trading
