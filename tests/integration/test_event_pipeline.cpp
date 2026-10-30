#include "market_data/normalized_csv_source.hpp"
#include "runtime/order_book_market_data_handler.hpp"
#include "replay/historical_replay.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>
using namespace trading;
#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (false)
const std::string header = "exchange_timestamp_ns,instrument_id,sequence,price,quantity,side,update_type\n";
struct Recorder : MarketDataSink {
    std::vector<MarketDataEvent> events;
    void on_event(const MarketDataEvent& e) override { events.push_back(e); }
};
struct ManualSource : MarketDataSource {
    explicit ManualSource(MarketDataSink& sink) : MarketDataSource(sink) {}
    void start() override { transition(ConnectionState::Connecting); transition(ConnectionState::Connected); }
    bool poll() override { return true; }
    void stop() override { transition(ConnectionState::Disconnected); }
    using MarketDataSource::transition;
    void send(const BookUpdate& e) { sink_.on_event(e); }
};
template<class F> void rejects(F f) { bool caught=false; try { f(); } catch (const std::exception&) { caught=true; } CHECK(caught); }
int main() {
    Recorder recorder;
    ManualSource manual{recorder};
    rejects([&]{manual.transition(ConnectionState::Connected);});
    manual.start();
    CHECK(std::get<ConnectionStateChanged>(recorder.events[0]).state==ConnectionState::Connecting);
    CHECK(std::get<ConnectionStateChanged>(recorder.events[1]).state==ConnectionState::Connected);
    const BookUpdate original{11, 12, 7, Side::Ask, 101, 5, UpdateType::Modify, 42};
    manual.send(original);
    const auto actual = std::get<BookUpdate>(recorder.events.back());
    CHECK(actual.exchange_time==11 && actual.receive_time==12 && actual.instrument==7 && actual.side==Side::Ask);
    CHECK(actual.price==101 && actual.quantity==5 && actual.type==UpdateType::Modify && actual.sequence==42);
    manual.transition(ConnectionState::Failed);
    rejects([&]{manual.transition(ConnectionState::Connected);});
    manual.stop(); manual.start(); manual.transition(ConnectionState::Completed); manual.stop();
    CHECK(manual.state()==ConnectionState::Disconnected);
    const std::string initial="10,1,100,100,10,BID,ADD\n10,1,101,102,20,ASK,ADD\n";
    const std::string changes="11,1,102,101,5,BID,ADD\n12,1,103,101,9,BID,MODIFY\n13,1,104,100,0,BID,DELETE\n";
    std::istringstream s{header+initial}, u{header+changes};
    OrderBookMarketDataHandler handler;
    NormalizedCsvMarketDataSource source{s,u,DecimalTickSize{"1"},handler};
    rejects([&]{(void)source.poll();});
    source.start(); while(source.poll()) {}
    CHECK(source.state()==ConnectionState::Completed && source.snapshot_events()==2 && source.update_events()==3);
    CHECK(!source.poll()); source.stop(); CHECK(handler.book()->state()==MarketDataState::Valid);
    rejects([&]{source.start();});
    // Independent direct parsing/apply reference, not the refactored replay facade.
    OrderBook direct{1}; direct.begin_sync();
    std::istringstream ds{header+initial}, du{header+changes};
    NormalizedCsvReader sr{ds,DecimalTickSize{"1"}}, ur{du,DecimalTickSize{"1"}};
    while(auto e=sr.next()) CHECK(direct.apply(*e)==BookError::None);
    CHECK(direct.finish_sync()==BookError::None);
    while(auto e=ur.next()) CHECK(direct.apply(*e)==BookError::None);
    CHECK(direct.best_bid()==handler.book()->best_bid() && direct.best_ask()==handler.book()->best_ask());
    CHECK(direct.last_sequence()==handler.book()->last_sequence());
    for(Price p=99;p<104;++p) {
        CHECK(direct.bid_volume(p)==handler.book()->bid_volume(p));
        CHECK(direct.ask_volume(p)==handler.book()->ask_volume(p));
    }
    for(const auto& [row,error] : std::vector<std::pair<std::string,BookError>>{
        {"11,1,104,101,1,BID,ADD\n",BookError::SequenceGap},
        {"11,1,102,102,1,BID,ADD\n",BookError::CrossedBook}}) {
        std::istringstream fs{header+initial},fu{header+row+"UNREAD\n"};
        OrderBookMarketDataHandler h;
        NormalizedCsvMarketDataSource f{fs,fu,DecimalTickSize{"1"},h}; f.start();
        try {while(f.poll()) {} CHECK(false);} catch(const BookDispatchError& e) {CHECK(e.book_error()==error);}
        CHECK(f.state()==ConnectionState::Failed && fu.peek()=='U');
        CHECK(h.book()->state()==MarketDataState::Invalid && h.book()->last_error()==error);
        rejects([&]{(void)f.poll();});
    }
    // Invalid normalized events fail at the same consumer boundary.
    handler.on_event(SnapshotBegin{1});
    try { handler.on_event(BookUpdate{0,{},1,Side::Bid,0,1,UpdateType::Add,0}); CHECK(false); }
    catch(const BookDispatchError& e) { CHECK(e.book_error()==BookError::InvalidEvent); }
    std::istringstream cs{header+initial},cu{header+changes};
    NormalizedCsvMarketDataSource cancelled{cs,cu,DecimalTickSize{"1"},handler};
    cancelled.start(); CHECK(cancelled.poll()); cancelled.stop();
    CHECK(cancelled.state()==ConnectionState::Disconnected && handler.book()->state()==MarketDataState::Empty);
    rejects([&]{(void)cancelled.poll();});
    std::istringstream bs{"bad header"},bu{header};
    NormalizedCsvMarketDataSource broken{bs,bu,DecimalTickSize{"1"},handler};
    rejects([&]{broken.start();}); CHECK(broken.state()==ConnectionState::Failed && !handler.book());
    // L1 quotes do not create a reconstructed book.
    handler.on_event(Quote{1,{},1,{}, {},QuoteMode::Realtime}); CHECK(!handler.book());
    {
        std::istringstream abandoned_snapshot{header+initial},abandoned_updates{header+changes};
        NormalizedCsvMarketDataSource abandoned{abandoned_snapshot,abandoned_updates,DecimalTickSize{"1"},handler};
        abandoned.start();
        CHECK(abandoned.poll() && abandoned.poll() && abandoned.poll());
        CHECK(handler.book()->state()==MarketDataState::Valid);
    }
    CHECK(handler.book()->state()==MarketDataState::Empty);
    struct RejectCompletion : MarketDataSink {
        void on_event(const MarketDataEvent& e) override {
            if(const auto* state=std::get_if<ConnectionStateChanged>(&e);
               state && state->state==ConnectionState::Completed) throw std::runtime_error("sink failed");
        }
    } reject;
    std::istringstream rs{header+initial},ru{header};
    NormalizedCsvMarketDataSource rejected{rs,ru,DecimalTickSize{"1"},reject};
    rejected.start(); rejects([&]{while(rejected.poll()) {}});
    CHECK(rejected.state()==ConnectionState::Failed);
    std::cout << "Event pipeline checks passed\n";
}
