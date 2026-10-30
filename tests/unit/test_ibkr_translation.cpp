#include "market_data/ibkr/quote_translator.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace trading;
#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while(false)
struct Sink : MarketDataSink {
    std::vector<Quote> quotes;
    void on_event(const MarketDataEvent& e) override {quotes.push_back(std::get<Quote>(e));}
};
template<class F> void rejects(F f) {bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
int main() {
    Sink sink; ibkr::QuoteTranslator t{7,0.01,sink};
    CHECK(t.to_ticks(100.01)==10001);
    CHECK(t.to_ticks(std::nextafter(100.01,101.0))==10001);
    CHECK(ibkr::QuoteTranslator(7,0.005,sink).to_ticks(100.025)==20005);
    for(double v : {0.0,-1.0,100.005,1e30,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&]{(void)t.to_ticks(v);});
    for(double tick : {0.0,-1.0,std::numeric_limits<double>::infinity()}) rejects([&]{ibkr::QuoteTranslator bad{7,tick,sink};});
    rejects([&]{t.price(1,100,0);});
    t.mode(1,0); t.price(1,100.01,10); t.size(0,"10.000",11); t.price(2,100.03,12); t.size(3,"20",13);
    auto q=sink.quotes.back();
    CHECK(q.instrument==7 && !q.exchange_time && q.receive_time==13 && q.mode==QuoteMode::Realtime);
    CHECK(q.bid.price==10001 && q.bid.quantity==10 && q.ask.price==10003 && q.ask.quantity==20);
    CHECK(q.bid.price_receive_time==10 && q.bid.quantity_receive_time==11);
    t.price(1,100.02,14); CHECK(!sink.quotes.back().bid.quantity);
    t.size(0,"0",15); CHECK(sink.quotes.back().bid.quantity==0);
    t.price(1,-1,16); CHECK(!sink.quotes.back().bid.price && !sink.quotes.back().bid.quantity);
    t.size(0,"-1",17); CHECK(!sink.quotes.back().bid.quantity);
    for(auto value : {"", "1.1", "-2", "4294967296", "1e2", "NaN", "1.", ".0", " 1"}) rejects([&]{t.size(0,value,18);});
    t.size(0,"4294967295",18); CHECK(sink.quotes.back().bid.quantity==std::numeric_limits<Quantity>::max());
    const auto n=sink.quotes.size(); t.price(4,123,18); t.size(5,"20",18); CHECK(sink.quotes.size()==n);
    t.mode(3,20); CHECK(!sink.quotes.back().ask.price && !sink.quotes.back().bid.price);
    t.price(66,10,21); t.size(69,"3",22); t.price(67,11,23); t.size(70,"4",24);
    CHECK(sink.quotes.back().bid.price==1000 && sink.quotes.back().ask.quantity==4);
    rejects([&]{t.price(1,10,25);}); rejects([&]{t.mode(5,25);}); rejects([&]{t.size(69,"1",-1);});
    t.mode(4,26); CHECK(sink.quotes.back().mode==QuoteMode::DelayedFrozen);
    t.mode(2,27); t.price(1,10,28); CHECK(sink.quotes.back().mode==QuoteMode::Frozen);
    t.reset(); rejects([&]{t.price(1,10,29);});
    std::cout << "IBKR translation checks passed\n";
}
