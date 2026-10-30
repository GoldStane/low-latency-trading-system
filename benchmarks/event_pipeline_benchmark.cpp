#include "runtime/order_book_market_data_handler.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>
using namespace trading;
using Clock = std::chrono::steady_clock;
struct CountingSink final : MarketDataSink {
    std::uint64_t checksum{};
    void on_event(const MarketDataEvent& event) override {
        const auto& update=std::get<BookUpdate>(event);
        checksum += update.quantity + update.sequence;
    }
};
// Out-of-line dispatch through the same virtual sink boundary used by sources.
// Separate translation unit implementations and observable checksums prevent dead work.
void measure(const char* name, MarketDataSink& sink) {
    constexpr std::size_t warmup=30000, measured=300000;
    std::vector<MarketDataEvent> events;
    events.reserve(warmup+measured);
    for(std::size_t i=0;i<warmup+measured;++i) {
        auto type=i%3==0 ? UpdateType::Add : i%3==1 ? UpdateType::Modify : UpdateType::Delete;
        events.emplace_back(BookUpdate{0,{},1,Side::Bid,101,type==UpdateType::Delete ? 0U : 5U,type,i+2});
    }
    std::vector<double> samples(measured);
    for(std::size_t i=0;i<warmup;++i) sink.on_event(events[i]);
    for(std::size_t i=0;i<measured;++i) {
        const auto& event=events[i+warmup];
        const auto start=Clock::now();
        sink.on_event(event);
        const auto end=Clock::now();
        samples[i]=std::chrono::duration<double,std::nano>(end-start).count();
    }
    const double total=std::accumulate(samples.begin(),samples.end(),0.0);
    std::sort(samples.begin(),samples.end());
    std::cout << name << ".individual_event_ns.p50=" << samples[measured/2-1] << '\n'
              << name << ".individual_event_ns.p95=" << samples[measured*95/100-1] << '\n'
              << name << ".individual_event_ns.p99=" << samples[measured*99/100-1] << '\n'
              << name << ".timed_region_events_per_second=" << 1e9*measured/total << '\n';
}
int main() {
    CountingSink counter;
    measure("dispatch",counter);
    OrderBookMarketDataHandler book;
    book.on_event(SnapshotBegin{1});
    book.on_event(BookUpdate{0,{},1,Side::Bid,100,1,UpdateType::Add,0});
    book.on_event(BookUpdate{0,{},1,Side::Ask,102,1,UpdateType::Add,1});
    book.on_event(SnapshotEnd{});
    measure("dispatch_book",book);
    std::cout << "dispatch.checksum=" << counter.checksum << '\n';
    return book.book()->state()==MarketDataState::Valid && book.book()->last_sequence()==330001 ? 0 : 1;
}
