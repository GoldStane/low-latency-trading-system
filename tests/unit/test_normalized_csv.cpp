#include "market_data/normalized_csv_reader.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

using namespace trading;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " << #condition << '\n'; \
    std::exit(1); } } while (false)

const std::string header = "exchange_timestamp_ns,instrument_id,sequence,price,quantity,side,update_type\n";

template<typename F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

void bad_csv(const std::string& text, std::size_t line = 2) {
    std::istringstream input{text};
    bool rejected = false;
    try {
        NormalizedCsvReader reader{input, DecimalTickSize{"0.01"}};
        while (reader.next()) {}
    } catch (const CsvError& error) {
        rejected = true;
        CHECK(error.line() == line);
        CHECK(std::string(error.what()).find("line " + std::to_string(line)) != std::string::npos);
    }
    CHECK(rejected);
}

int main() {
    const DecimalTickSize cent{"0.01"};
    CHECK(cent.to_ticks("100.02") == 10002);
    CHECK(DecimalTickSize{"0.005"}.to_ticks("100.025") == 20005);
    CHECK(DecimalTickSize{"2.50"}.to_ticks("100.0000") == 40);
    CHECK(DecimalTickSize{"000.0100"}.to_ticks("000100.0200") == 10002);
    CHECK(cent.to_ticks("92233720368547758.07") == std::numeric_limits<Price>::max());
    CHECK(DecimalTickSize{"1"}.to_ticks("9223372036854775807") == std::numeric_limits<Price>::max());
    const std::string huge(64, '9');
    CHECK(DecimalTickSize{huge}.to_ticks(huge) == 1);
    const std::string tiny = "0." + std::string(61, '0') + "1";
    CHECK(DecimalTickSize{tiny}.to_ticks(tiny) == 1);
    CHECK(DecimalTickSize{"0.00000000000000000001"}.to_ticks("0.09223372036854775807")
          == std::numeric_limits<Price>::max());
    for (const auto& value : {"", "0", "0.0", "-1", "+1", " 1", "1 ", "1e2", "NaN", "Infinity",
                             ".1", "1.", "1..2", "1.2.3", "1\t", "1\n", "1,2"}) {
        rejects([&] { (void)cent.to_ticks(value); });
        rejects([&] { (void)DecimalTickSize{value}; });
    }
    for (const auto& value : {"100.025", "92233720368547758.08", "0.001"})
        rejects([&] { (void)cent.to_ticks(value); });
    rejects([&] { (void)cent.to_ticks(std::string(65, '1')); });
    rejects([&] { (void)DecimalTickSize{std::string(65, '1')}; });
    rejects([&] { (void)DecimalTickSize{tiny}.to_ticks(huge); });
    rejects([&] { (void)DecimalTickSize{huge}.to_ticks(tiny); });

    std::istringstream input{header + "0,0,0,100.02,1,BID,ADD\n"
        "9223372036854775807,4294967295,18446744073709551615,92233720368547758.07,4294967295,ASK,MODIFY\n"
        "1,1,1,100,0,BID,DELETE"};
    NormalizedCsvReader reader{input, cent};
    const auto add = reader.next();
    CHECK(add && add->exchange_time == 0 && add->instrument == 0 && add->sequence == 0);
    CHECK(add->side == Side::Bid && add->type == UpdateType::Add && add->quantity == 1 && add->price == 10002);
    CHECK(!add->receive_time);
    CHECK(input.peek() == '9'); // Only one event has been consumed.
    const auto modify = reader.next();
    CHECK(modify && modify->exchange_time == std::numeric_limits<Timestamp>::max());
    CHECK(modify->instrument == std::numeric_limits<InstrumentId>::max());
    CHECK(modify->sequence == std::numeric_limits<SequenceNumber>::max());
    CHECK(modify->quantity == std::numeric_limits<Quantity>::max());
    CHECK(modify->price == std::numeric_limits<Price>::max());
    CHECK(modify->side == Side::Ask && modify->type == UpdateType::Modify && !modify->receive_time);
    const auto del = reader.next();
    CHECK(del && del->type == UpdateType::Delete && del->quantity == 0 && del->sequence == 1);
    CHECK(!reader.next() && !reader.next()); // Reader does not impose chronology.

    std::istringstream quoted{header + "\r\n\"10\",\"1\",\"1\",\"100.02\",\"2\",\"ASK\",\"ADD\"\r\n"};
    NormalizedCsvReader quotes{quoted, cent};
    CHECK(quotes.next()->price == 10002);
    CHECK(quotes.line() == 3 && !quotes.next());
    std::istringstream quoted_header{"\"exchange_timestamp_ns\",instrument_id,sequence,price,quantity,side,update_type\r\n"};
    NormalizedCsvReader empty{quoted_header, cent};
    CHECK(!empty.next()); // Empty increments are legal; snapshot completeness is replay's job.

    for (const auto& text : {std::string{}, std::string{"wrong\n"},
                            std::string{"instrument_id,exchange_timestamp_ns,sequence,price,quantity,side,update_type\n"},
                            " " + header, header.substr(0, header.size()-2) + "\n"}) {
        bad_csv(text, 1);
    }
    const std::array<std::string, 7> valid{"10", "1", "1", "100", "1", "BID", "ADD"};
    const std::array<std::vector<std::string>, 7> invalid{{
        {"", "-1", "+1", "1.0", "1a", " 1", "1 ", "9223372036854775808", "000000000000000000000"},
        {"", "-1", "4294967296"},
        {"", "-1", "18446744073709551616"},
        {"", "-1", "0", "1e2", "100.025", "1.", ".1", "NaN", " 100", "92233720368547758.08"},
        {"", "-1", "0", "1.5", "4294967296"},
        {"", "bid", "BUY", " BID", "ASK "},
        {"", "add", "REPLACE", " ADD"}
    }};
    for (std::size_t column = 0; column < valid.size(); ++column) {
        for (const auto& value : invalid[column]) {
            auto row = valid;
            row[column] = value;
            std::string text;
            for (std::size_t i = 0; i < row.size(); ++i) text += (i ? "," : "") + row[i];
            bad_csv(header + text + '\n');
        }
    }
    for (const auto& row : {"10,1,1,100,1,BID", "10,1,1,100,1,BID,ADD,extra",
                            "10,1,1,100,0,ASK,MODIFY", "10,1,1,100,1,ASK,DELETE",
                            "10,1,1,\"100,1,BID,ADD", "10,1,1,\"100\"x,1,BID,ADD",
                            "10,1,1,10\"0,1,BID,ADD", "10,1,1,\"10\"\"0\",1,BID,ADD",
                            "10,1,1,\"100,00\",1,BID,ADD", " "})
        bad_csv(header + row + '\n');
    bad_csv(header + "\n10,1,1,,1,BID,ADD\n", 3);
    bad_csv(header + std::string(1025, '0'));
    std::istringstream broken{header};
    NormalizedCsvReader io{broken, cent};
    broken.setstate(std::ios::badbit);
    bool failed = false;
    try { (void)io.next(); } catch (const CsvError&) { failed = true; }
    CHECK(failed);
    std::istringstream exceptions{header + "1,1,1,1,1,BID,ADD"};
    exceptions.exceptions(std::ios::failbit | std::ios::badbit);
    NormalizedCsvReader throwing{exceptions, cent};
    CHECK(throwing.next()->price == 100);
    CHECK(!throwing.next());
    std::cout << "Normalized CSV checks passed\n";
}
