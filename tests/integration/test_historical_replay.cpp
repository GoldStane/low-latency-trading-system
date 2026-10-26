#include "replay/historical_replay.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace trading;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " << #condition << '\n'; \
    std::exit(1); } } while (false)

const std::string header = "exchange_timestamp_ns,instrument_id,sequence,price,quantity,side,update_type\n";
const std::string snapshot = "10,1,100,99.99,10,BID,ADD\n10,1,101,100.00,20,BID,ADD\n"
                             "10,1,102,100.02,30,ASK,ADD\n10,1,103,100.03,40,ASK,ADD\n";
const std::string updates = "11,1,104,100.01,15,BID,ADD\n12,1,105,100.01,25,BID,MODIFY\n"
                            "13,1,106,100.02,0,ASK,DELETE\n14,1,107,100.04,50,ASK,ADD\n"
                            "15,1,108,100.00,0,BID,DELETE\n16,1,109,100.03,35,ASK,MODIFY\n";

ReplaySummary run(HistoricalReplay& replay, const std::string& initial, const std::string& changes) {
    std::istringstream s{header + initial}, u{header + changes};
    return replay.run(s, u, DecimalTickSize{"0.01"});
}

void fails(const std::string& initial, const std::string& changes, BookError expected,
           const std::string& location) {
    HistoricalReplay replay;
    bool failed = false;
    try { (void)run(replay, initial, changes); }
    catch (const ReplayError& error) {
        failed = true;
        CHECK(error.book_error() == expected);
        CHECK(std::string(error.what()).find(location) != std::string::npos);
    }
    CHECK(failed);
    if (replay.book()) {
        const auto& book = *replay.book();
        CHECK(book.state() == MarketDataState::Invalid);
        CHECK(book.last_error() == expected);
        CHECK(book.bid_levels() == 0 && book.ask_levels() == 0);
        CHECK(!book.best_bid() && !book.best_ask() && book.invariants_hold());
    }
    CHECK(run(replay, snapshot, updates).update_events == 6); // Explicit fresh session recovers.
}

int main() {
    HistoricalReplay first, second;
    const auto summary = run(first, snapshot, updates);
    CHECK(summary == (ReplaySummary{1, 4, 6}));
    CHECK(summary == run(second, snapshot, updates));
    const auto& a = *first.book();
    const auto& b = *second.book();
    CHECK(a.best_bid() == (PriceLevel{10001, 25}));
    CHECK(a.best_ask() == (PriceLevel{10003, 35}));
    CHECK(a.bid_levels() == 2 && a.ask_levels() == 2 && a.last_sequence() == 109);
    CHECK(a.state() == MarketDataState::Valid && a.invariants_hold());
    CHECK(a.bid_volume(9999) == 10 && a.ask_volume(10004) == 50);
    CHECK(a.bid_volume(10000) == 0 && a.ask_volume(10002) == 0);
    CHECK(a.best_bid() == b.best_bid() && a.best_ask() == b.best_ask());
    CHECK(a.bid_levels() == b.bid_levels() && a.ask_levels() == b.ask_levels());
    CHECK(a.last_sequence() == b.last_sequence() && a.state() == b.state() && a.last_error() == b.last_error());
    for (Price price = 9998; price <= 10005; ++price) {
        CHECK(a.bid_volume(price) == b.bid_volume(price));
        CHECK(a.ask_volume(price) == b.ask_volume(price));
    }
    CHECK(run(second, snapshot, "").update_events == 0);
    CHECK(second.book()->state() == MarketDataState::Valid && second.book()->last_sequence() == 103);
    // Valid can become one-sided; replay must not strengthen/change book semantics.
    (void)run(second, "0,1,0,1,1,BID,ADD\n0,1,1,2,1,ASK,ADD\n", "0,1,2,2,0,ASK,DELETE\n");
    CHECK(second.book()->state() == MarketDataState::Valid && !second.book()->best_ask());

    fails("", "", BookError::IncompleteBook, "snapshot line 1");
    fails("10,1,1,100,1,BID,ADD\n", "", BookError::IncompleteBook, "snapshot line 2");
    fails("10,1,1,102,1,BID,ADD\n10,1,2,101,1,ASK,ADD\n", "", BookError::CrossedBook, "snapshot line 3");
    fails("10,1,1,101,1,BID,ADD\n10,1,2,101,1,ASK,ADD\n", "", BookError::CrossedBook, "snapshot line 3");
    fails("10,1,1,100,1,BID,ADD\n10,2,2,101,1,ASK,ADD\n", "", BookError::WrongInstrument, "snapshot line 3");
    fails("10,1,1,100,1,BID,ADD\n10,1,3,101,1,ASK,ADD\n", "", BookError::SequenceGap, "snapshot line 3");
    fails("10,1,1,100,1,BID,ADD\n10,1,2,100,1,BID,ADD\n", "", BookError::DuplicateLevel, "snapshot line 3");
    fails("10,1,1,100,1,BID,ADD\n9,1,2,101,1,ASK,ADD\n", "", BookError::TimeRegression, "snapshot line 3");
    for (const auto& sequence : {"102", "103", "105"})
        fails(snapshot, "11,1," + std::string(sequence) + ",100.01,1,BID,ADD\n", BookError::SequenceGap, "updates line 2");
    fails(snapshot, "11,2,104,100.01,1,BID,ADD\n", BookError::WrongInstrument, "updates line 2");
    fails(snapshot, "11,1,104,100,1,BID,ADD\n", BookError::DuplicateLevel, "updates line 2");
    fails(snapshot, "11,1,104,99,1,BID,MODIFY\n", BookError::MissingLevel, "updates line 2");
    fails(snapshot, "11,1,104,101,0,ASK,DELETE\n", BookError::MissingLevel, "updates line 2");
    fails(snapshot, "9,1,104,99,1,BID,ADD\n", BookError::TimeRegression, "updates line 2");
    fails(snapshot, "11,1,104,100.02,1,BID,ADD\n", BookError::CrossedBook, "updates line 2");
    fails("0,1,18446744073709551614,1,1,BID,ADD\n0,1,18446744073709551615,2,1,ASK,ADD\n",
          "0,1,0,3,1,ASK,ADD\n", BookError::SequenceGap, "updates line 2");

    // Stop at the first failure: the following malformed event must remain unread.
    std::istringstream s{header + snapshot}, u{header + "11,1,104,100,1,BID,ADD\nBAD\n"};
    try { (void)second.run(s, u, DecimalTickSize{"0.01"}); CHECK(false); }
    catch (const ReplayError&) { CHECK(u.peek() == 'B'); }
    CHECK(second.book()->last_sequence() == 103);

    for (bool bad_snapshot : {false, true}) {
        HistoricalReplay broken;
        try {
            (void)run(broken, bad_snapshot ? snapshot + "11,1,104,100,1,BID,MODIFY\n" : snapshot,
                      bad_snapshot ? "" : "11,1,104,100,1,BID,MODIFY\nmalformed\n");
            CHECK(false);
        } catch (const std::runtime_error& error) {
            CHECK(std::string(error.what()).find(bad_snapshot ? "snapshot line 6" : "updates line 3") != std::string::npos);
        }
        CHECK(broken.book()->state() == MarketDataState::Empty);
        CHECK(!broken.book()->best_bid() && !broken.book()->best_ask());
    }
    std::cout << "Historical replay checks passed\n";
}
