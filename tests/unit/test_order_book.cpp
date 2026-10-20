#include "order_book/order_book.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace trading;

// Unlike assert(), CHECK stays active in Release builds.
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " << #condition << '\n'; \
    std::exit(1); } } while (false)

BookUpdate event(SequenceNumber sequence, Side side, Price price, Quantity quantity,
                 UpdateType type = UpdateType::Add) {
    return {100, std::nullopt, 7, side, price, quantity, type, sequence};
}

OrderBook ready() {
    OrderBook book{7};
    book.begin_sync();
    CHECK(book.apply(event(10, Side::Bid, 100, 5)) == BookError::None);
    CHECK(book.apply(event(11, Side::Ask, 102, 6)) == BookError::None);
    CHECK(book.finish_sync() == BookError::None);
    return book;
}

void check_invalid(const OrderBook& book) {
    CHECK(book.state() == MarketDataState::Invalid);
    CHECK(!book.best_bid() && !book.best_ask());
    CHECK(book.invariants_hold());
}

int main() {
    OrderBook empty{7};
    CHECK(empty.state() == MarketDataState::Empty);
    CHECK(empty.invariants_hold());
    CHECK(empty.bid_volume(100) == 0);
    CHECK(empty.apply(event(0, Side::Bid, 100, 5)) == BookError::NotReady);
    check_invalid(empty);
    empty.begin_sync();
    CHECK(empty.finish_sync() == BookError::IncompleteBook);
    check_invalid(empty);

    auto book = ready();
    CHECK(book.best_bid() == (PriceLevel{100, 5}));
    CHECK(book.best_ask() == (PriceLevel{102, 6}));
    CHECK(book.apply(event(12, Side::Bid, 99, 7)) == BookError::None);
    CHECK(book.apply(event(13, Side::Bid, 100, 9, UpdateType::Modify)) == BookError::None);
    CHECK(book.bid_volume(100) == 9); // Replacement, not addition.
    CHECK(book.apply(event(14, Side::Bid, 100, 0, UpdateType::Delete)) == BookError::None);
    CHECK(book.best_bid() == (PriceLevel{99, 7}));
    CHECK(book.apply(event(15, Side::Ask, 103, 8)) == BookError::None);
    CHECK(book.apply(event(16, Side::Ask, 102, 0, UpdateType::Delete)) == BookError::None);
    CHECK(book.best_ask() == (PriceLevel{103, 8}));
    CHECK(book.apply(event(17, Side::Ask, 103, 0, UpdateType::Delete)) == BookError::None);
    CHECK(!book.best_ask()); // An empty side is consistent, but cannot quote a spread.
    CHECK(book.state() == MarketDataState::Valid && book.invariants_hold());
    CHECK(book.apply(event(18, Side::Ask, 104, 3)) == BookError::None);
    CHECK(book.apply(event(19, Side::Ask, 104, 11, UpdateType::Modify)) == BookError::None);
    CHECK(book.ask_volume(104) == 11);
    CHECK(book.best_ask() == (PriceLevel{104, 11}));
    book.clear();
    CHECK(book.state() == MarketDataState::Empty && !book.last_sequence());

    const auto fails = [](BookUpdate update, BookError error) {
        auto target = ready();
        CHECK(target.apply(update) == error);
        CHECK(target.last_error() == error);
        CHECK(target.last_sequence() == 11);
        check_invalid(target);
        CHECK(target.apply(event(12, Side::Bid, 99, 1)) == BookError::NotReady);
        target.begin_sync();
        CHECK(target.apply(event(100, Side::Bid, 90, 1)) == BookError::None);
        CHECK(target.apply(event(101, Side::Ask, 91, 1)) == BookError::None);
        CHECK(target.finish_sync() == BookError::None);
    };
    fails(event(12, Side::Bid, 100, 1), BookError::DuplicateLevel);
    fails(event(12, Side::Bid, 99, 1, UpdateType::Modify), BookError::MissingLevel);
    fails(event(12, Side::Ask, 103, 0, UpdateType::Delete), BookError::MissingLevel);
    for (auto sequence : {10, 11, 13})
        fails(event(static_cast<SequenceNumber>(sequence), Side::Bid, 99, 1), BookError::SequenceGap);
    fails(event(12, Side::Bid, 102, 1), BookError::CrossedBook);
    fails(event(12, Side::Ask, 99, 1), BookError::CrossedBook);
    fails(event(12, Side::Bid, 99, 0), BookError::InvalidEvent);
    fails(event(12, Side::Bid, 100, 1, UpdateType::Delete), BookError::InvalidEvent);
    fails(event(12, Side::Bid, 100, 0, UpdateType::Modify), BookError::InvalidEvent);
    fails(event(12, Side::Bid, -1, 1), BookError::InvalidEvent);
    fails(event(12, Side::Bid, 0, 1), BookError::InvalidEvent);
    fails(event(12, static_cast<Side>(255), 99, 1), BookError::InvalidEvent);
    fails(event(12, Side::Bid, 99, 1, static_cast<UpdateType>(255)), BookError::InvalidEvent);
    auto bad = event(12, Side::Bid, 99, 1);
    bad.instrument = 8;
    fails(bad, BookError::WrongInstrument);
    bad.instrument = 7;
    bad.exchange_time = 99;
    fails(bad, BookError::TimeRegression);
    bad.exchange_time = -1;
    fails(bad, BookError::InvalidEvent);
    bad.exchange_time = 100;
    bad.receive_time = -1;
    fails(bad, BookError::InvalidEvent);

    OrderBook crossed{7};
    crossed.begin_sync();
    CHECK(crossed.apply(event(0, Side::Bid, 102, 1)) == BookError::None);
    CHECK(crossed.apply(event(1, Side::Ask, 101, 1)) == BookError::None);
    CHECK(crossed.finish_sync() == BookError::CrossedBook);
    check_invalid(crossed);
    CHECK(crossed.finish_sync() == BookError::NotSyncing);

    // Snapshot construction can repair an intermediate cross before completion.
    crossed.begin_sync();
    CHECK(crossed.apply(event(0, Side::Bid, 102, 1)) == BookError::None);
    CHECK(crossed.apply(event(1, Side::Ask, 101, 1)) == BookError::None);
    CHECK(crossed.state() == MarketDataState::Syncing);
    CHECK(crossed.apply(event(2, Side::Bid, 102, 0, UpdateType::Delete)) == BookError::None);
    CHECK(crossed.apply(event(3, Side::Bid, 100, 1)) == BookError::None);
    CHECK(crossed.finish_sync() == BookError::None);

    OrderBook bounds{7};
    bounds.begin_sync();
    CHECK(bounds.apply(event(std::numeric_limits<SequenceNumber>::max(), Side::Ask,
                            std::numeric_limits<Price>::max(), std::numeric_limits<Quantity>::max())) == BookError::None);
    CHECK(bounds.ask_volume(std::numeric_limits<Price>::max()) == std::numeric_limits<Quantity>::max());
    CHECK(bounds.apply(event(0, Side::Bid, 1, 1)) == BookError::SequenceGap);

    // Deterministic repeated transitions verify every observable best level.
    auto first = ready();
    auto second = ready();
    SequenceNumber sequence = 12;
    for (Quantity quantity = 1; quantity <= 1000; ++quantity) {
        for (const auto update : {event(sequence++, Side::Bid, 101, quantity),
                                  event(sequence++, Side::Bid, 101, quantity + 1, UpdateType::Modify),
                                  event(sequence++, Side::Bid, 101, 0, UpdateType::Delete)}) {
            CHECK(first.apply(update) == BookError::None);
            CHECK(second.apply(update) == BookError::None);
            CHECK(first.invariants_hold() && second.invariants_hold());
            CHECK(first.best_bid() == second.best_bid());
            CHECK(first.best_bid() == (update.type == UpdateType::Delete
                  ? PriceLevel{100, 5} : PriceLevel{101, update.quantity}));
            CHECK(first.best_ask() == (PriceLevel{102, 6}));
        }
    }
    std::cout << "Order book checks passed\n";
}
