#include "market_data/ibkr/ibkr_source.hpp"
#include "market_data/ibkr/quote_translator.hpp"
#include "Decimal.h"
#include <cstdlib>
#include <iostream>
using namespace trading;
#define CHECK(x) do { if (!(x)) { std::cerr<<"line "<<__LINE__<<": "<<#x<<'\n';std::exit(1); } } while(false)
struct Sink : MarketDataSink {
    Quote quote{};
    void on_event(const MarketDataEvent& e) override { if(auto* q=std::get_if<Quote>(&e))quote=*q; }
};
int main() {
    Sink sink;
    ibkr::QuoteTranslator t{1,0.01,sink}; t.mode(1,0);
    for(const auto& text : {"0","1","10","4294967295"}) {
        const auto decimal=DecimalFunctions::stringToDecimal(text);
        const auto display=DecimalFunctions::decimalStringToDisplay(decimal);
        t.size(0,display,1);
        CHECK(sink.quote.bid.quantity==std::stoull(text));
    }
    t.size(0,DecimalFunctions::decimalStringToDisplay(DecimalFunctions::stringToDecimal("-1")),2);
    CHECK(!sink.quote.bid.quantity);
    bool rejected=false;
    try {t.size(0,DecimalFunctions::decimalStringToDisplay(DecimalFunctions::stringToDecimal("1.5")),3);}
    catch(const std::invalid_argument&) {rejected=true;} CHECK(rejected);
    ibkr::Config config; config.symbol="AAPL";
    ibkr::IbkrMarketDataSource source{config,sink};
    CHECK(source.state()==ConnectionState::Disconnected);
    source.stop(); source.stop(); // Destructor/stop without a reader must be safe.
    config.port=0; rejected=false;
    try {ibkr::IbkrMarketDataSource invalid{config,sink};} catch(const std::invalid_argument&) {rejected=true;}
    CHECK(rejected);
    std::cout<<"IBKR SDK checks passed (no connection opened)\n";
}
