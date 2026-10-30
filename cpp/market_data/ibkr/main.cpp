#include "market_data/ibkr/ibkr_source.hpp"
#include "market_data/normalized_csv_reader.hpp"
#include <charconv>
#include <csignal>
#include <iostream>
#include <set>
#include <string_view>
using namespace trading;
namespace {
volatile std::sig_atomic_t stopping=0;
extern "C" void stop_signal(int) { stopping=1; }
template<class T> T number(std::string_view text) {
    T result{};
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),result);
    if(error!=std::errc{} || end!=text.data()+text.size() || text.empty()) throw std::invalid_argument("invalid numeric option");
    return result;
}
const char* connection_name(ConnectionState state) {
    switch(state) {
        case ConnectionState::Disconnected:return "DISCONNECTED";
        case ConnectionState::Connecting:return "CONNECTING";
        case ConnectionState::Connected:return "CONNECTED";
        case ConnectionState::Completed:return "COMPLETED";
        case ConnectionState::Failed:return "FAILED";
    }
    return "UNKNOWN";
}
struct Printer final : MarketDataSink {
    void on_event(const MarketDataEvent& event) override {
        if(const auto* c=std::get_if<ConnectionStateChanged>(&event)) std::cout<<"connection="<<connection_name(c->state)<<'\n';
        if(const auto* q=std::get_if<Quote>(&event)) {
            std::cout<<"instrument="<<q->instrument<<" mode="<<static_cast<int>(q->mode)
                     <<" receive_time_ns="<<q->receive_time<<" exchange_time_ns=none";
            const auto side=[](const char* name,const QuoteSide& value) {
                std::cout<<' '<<name<<"_ticks="<<(value.price?std::to_string(*value.price):"none")
                         <<' '<<name<<"_qty="<<(value.quantity?std::to_string(*value.quantity):"none");
            };
            side("bid",q->bid);side("ask",q->ask);std::cout<<'\n';
        }
        std::cout.flush();
    }
};
}
int main(int argc,char** argv) {
    try {
        ibkr::Config config;
        std::set<std::string> seen;
        for(int i=1;i<argc;i+=2) {
            const std::string key=argv[i];
            if(key=="--help" && argc==2) {
                std::cout<<"ibkr_market_data --symbol SYMBOL [--host HOST --port 7497 --client-id 7 --exchange SMART "
                    "--primary-exchange EXCHANGE --currency USD --instrument-id 1 --tick-size 0.01 "
                    "--market-data-mode 3 --startup-timeout-seconds 15]\nStocks only. No order execution.\n"; return 0;
            }
            if(i+1==argc || std::string_view(argv[i+1]).empty() || !seen.insert(key).second)
                throw std::invalid_argument("missing, empty, or duplicate option: "+key);
            const std::string value=argv[i+1];
            if(key=="--host")config.host=value;
            else if(key=="--port")config.port=number<int>(value);
            else if(key=="--client-id")config.client_id=number<int>(value);
            else if(key=="--symbol")config.symbol=value;
            else if(key=="--exchange")config.exchange=value;
            else if(key=="--primary-exchange")config.primary_exchange=value;
            else if(key=="--currency")config.currency=value;
            else if(key=="--instrument-id")config.instrument=number<InstrumentId>(value);
            else if(key=="--tick-size") { (void)DecimalTickSize{value}; config.tick_size=number<double>(value); }
            else if(key=="--market-data-mode")config.market_data_mode=number<int>(value);
            else if(key=="--startup-timeout-seconds")config.startup_timeout_seconds=number<int>(value);
            else throw std::invalid_argument("unknown option: "+key);
        }
        Printer printer;
        ibkr::IbkrMarketDataSource source{config,printer};
        std::signal(SIGINT,stop_signal); std::signal(SIGTERM,stop_signal);
        source.start();
        while(!stopping && source.poll()) {}
        source.stop();
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"IBKR market data failed: "<<e.what()<<'\n'; return 1;
    }
}
