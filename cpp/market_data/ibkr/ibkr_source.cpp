#include "market_data/ibkr/ibkr_source.hpp"
#include "market_data/ibkr/quote_translator.hpp"
#include "DefaultEWrapper.h"
#include "EClientSocket.h"
#include "EReader.h"
#include "EReaderOSSignal.h"
#include "Contract.h"
#include "Decimal.h"
#include <chrono>
#include <cmath>
#include <utility>
#include <atomic>
#include <mutex>
#include <sys/socket.h>

namespace trading::ibkr {
namespace {
Timestamp received_now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr int contract_request=1, quote_request=2;
}
class IbkrMarketDataSource::Impl final : public DefaultEWrapper {
public:
    Impl(IbkrMarketDataSource& owner, Config config, MarketDataSink& sink)
        : owner(owner), config(std::move(config)), translator(this->config.instrument,this->config.tick_size,sink),
          signal(100), client(this,&signal) {
        if (this->config.host.empty() || this->config.symbol.empty() || this->config.exchange.empty() ||
            this->config.currency.empty() || this->config.port<1 || this->config.port>65535 ||
            this->config.client_id<0 || this->config.market_data_mode<1 || this->config.market_data_mode>4 ||
            this->config.startup_timeout_seconds<1 || this->config.startup_timeout_seconds>300)
            throw std::invalid_argument("invalid IBKR connection/instrument configuration");
    }
    ~Impl() override { close(); }
    void close() noexcept {
        // Disconnect wakes the reader; its destructor joins before other SDK objects die.
        try { if (subscribed && client.isConnected()) client.cancelMktData(quote_request); } catch (...) {}
        subscribed=false;
        // Wake even a reader waiting for the remainder of a partial message.
        // Cancellation is sent first; shutdown then bounds the reader join.
        if (client.isSocketOK()) ::shutdown(client.fd(), SHUT_RDWR);
        client.eDisconnect();
        client.registerEReader(nullptr);
        reader.reset();
        translator.reset();
    }
    void nextValidId(int) override {
        check_failure();
        // API-ready handshake only. No order IDs are stored or used.
        if (ready) return;
        ready=true;
        owner.transition(ConnectionState::Connected);
        Contract request;
        request.symbol=config.symbol;
        request.secType="STK";
        request.exchange=config.exchange;
        request.primaryExchange=config.primary_exchange;
        request.currency=config.currency;
        client.reqContractDetails(contract_request,request);
    }
    void contractDetails(int id,const ContractDetails& details) override {
        check_failure();
        if(id!=contract_request || subscribed) return;
        ++contracts;
        if(contracts>1) throw std::runtime_error("ambiguous IBKR contract; configure primary exchange");
        contract=details.contract;
    }
    void contractDetailsEnd(int id) override {
        check_failure();
        if(id!=contract_request || subscribed) return;
        if(contracts!=1) throw std::runtime_error("IBKR contract did not resolve uniquely");
        client.reqMarketDataType(config.market_data_mode);
        subscribed=true;
        client.reqMktData(quote_request,contract,"",false,false,TagValueListSPtr{});
    }
    void marketDataType(int id,int mode) override {
        check_failure();
        if(id==quote_request && subscribed) translator.mode(mode,received_now());
    }
    void tickPrice(int id,TickType field,double price,const TickAttrib&) override {
        check_failure();
        if(id!=quote_request || !subscribed) return;
        translator.price(static_cast<int>(field),price,received_now());
        if(field==BID || field==ASK || field==DELAYED_BID || field==DELAYED_ASK) received_quote=true;
    }
    void tickSize(int id,TickType field,Decimal size) override {
        check_failure();
        if(id==quote_request && subscribed)
            translator.size(static_cast<int>(field),DecimalFunctions::decimalStringToDisplay(size),received_now());
    }
    // Socket errors can arrive from the SDK reader thread. Never throw there,
    // mutate core state, or deliver to the sink. The poll owner surfaces them.
    void connectionClosed() override { failed.store(true); }
    void error(int id,long long,int code,const std::string& message,const std::string&) override {
        if(code==2104 || code==2106 || code==2107 || code==2108 || code==2158) return;
        try {
            std::lock_guard lock(error_mutex);
            if(!failed.load()) failure_message="IBKR request="+std::to_string(id)+" code="+std::to_string(code)+": "+message;
        } catch(...) {}
        failed.store(true);
    }
    void check_failure() {
        if(!failed.load()) return;
        std::lock_guard lock(error_mutex);
        throw std::runtime_error(failure_message.empty()?"IBKR socket/API failure":failure_message);
    }
    IbkrMarketDataSource& owner;
    Config config;
    QuoteTranslator translator;
    EReaderOSSignal signal;
    EClientSocket client;
    std::unique_ptr<EReader> reader;
    Contract contract;
    bool started{},ready{},subscribed{},received_quote{};
    int contracts{};
    std::atomic<bool> failed{false};
    std::mutex error_mutex;
    std::string failure_message;
    std::chrono::steady_clock::time_point deadline;
};
IbkrMarketDataSource::IbkrMarketDataSource(Config config,MarketDataSink& sink)
    : MarketDataSource(sink),impl_(std::make_unique<Impl>(*this,std::move(config),sink)) {}
IbkrMarketDataSource::~IbkrMarketDataSource() {
    try { stop(); } catch (...) {} // Close resources even when a sink rejects notification.
}
void IbkrMarketDataSource::start() {
    if(impl_->started) throw std::logic_error("IBKR source is one-shot; construct a fresh session");
    impl_->started=true;
    try {
        transition(ConnectionState::Connecting);
        impl_->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(impl_->config.startup_timeout_seconds);
        if(!impl_->client.eConnect(impl_->config.host.c_str(),impl_->config.port,impl_->config.client_id))
            throw std::runtime_error("cannot connect to TWS/IB Gateway");
        impl_->reader=std::make_unique<EReader>(&impl_->client,&impl_->signal);
        impl_->reader->start();
    } catch(...) {
        impl_->close();
        try {transition(ConnectionState::Failed);} catch(...) {}
        throw;
    }
}
bool IbkrMarketDataSource::poll() {
    if(state()!=ConnectionState::Connecting && state()!=ConnectionState::Connected)
        throw std::logic_error("IBKR source is not active");
    try {
        impl_->check_failure();
        if(!impl_->client.isConnected()) throw std::runtime_error("IBKR socket disconnected");
        impl_->signal.waitForSignal(); // bounded at 100 ms; callbacks run here, not on reader thread
        impl_->reader->processMsgs();
        impl_->check_failure();
        if(!impl_->received_quote && std::chrono::steady_clock::now()>=impl_->deadline)
            throw std::runtime_error("IBKR startup timed out before first bid/ask callback");
        return true;
    } catch(...) {
        impl_->close();
        try {transition(ConnectionState::Failed);} catch(...) {}
        throw;
    }
}
void IbkrMarketDataSource::stop() {
    impl_->close();
    transition(ConnectionState::Disconnected);
}
} // namespace trading::ibkr
