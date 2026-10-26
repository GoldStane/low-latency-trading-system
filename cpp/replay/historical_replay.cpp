#include "replay/historical_replay.hpp"

#include <limits>

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

void count(std::uint64_t& value) {
    if (value == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("event count overflow");
    ++value;
}
} // namespace

ReplayError::ReplayError(const std::string& phase, std::size_t line, BookError error)
    : std::runtime_error(phase + " line " + std::to_string(line) + ": " + name(error)), error_(error) {}

ReplaySummary HistoricalReplay::run(std::istream& snapshot, std::istream& updates,
                                     const DecimalTickSize& tick_size) {
    book_.reset();
    ReplaySummary summary{};
    std::string phase = "snapshot";
    try {
        NormalizedCsvReader initial{snapshot, tick_size};
        while (const auto update = initial.next()) {
            if (!book_) {
                summary.instrument = update->instrument;
                book_.emplace(update->instrument);
                book_->begin_sync();
            }
            if (update->type != UpdateType::Add)
                throw CsvError(initial.line(), "snapshot rows must use ADD");
            const auto error = book_->apply(*update);
            if (error != BookError::None) throw ReplayError(phase, initial.line(), error);
            count(summary.snapshot_events);
        }
        if (!book_) throw ReplayError(phase, initial.line(), BookError::IncompleteBook);
        const auto error = book_->finish_sync();
        if (error != BookError::None) throw ReplayError(phase, initial.line(), error);

        phase = "updates";
        NormalizedCsvReader incremental{updates, tick_size};
        while (const auto update = incremental.next()) {
            const auto result = book_->apply(*update);
            if (result != BookError::None) throw ReplayError(phase, incremental.line(), result);
            count(summary.update_events);
        }
        return summary;
    } catch (const CsvError& error) {
        if (book_) book_->clear();
        throw std::runtime_error(phase + " " + error.what());
    } catch (...) {
        if (book_ && book_->state() != MarketDataState::Invalid) book_->clear();
        throw;
    }
}
} // namespace trading
