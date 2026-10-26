#include "market_data/normalized_csv_reader.hpp"

#include <array>
#include <limits>
#include <utility>

namespace trading {
namespace {
constexpr std::array<std::string_view, 7> header{
    "exchange_timestamp_ns", "instrument_id", "sequence", "price",
    "quantity", "side", "update_type"};

bool digit(char c) { return c >= '0' && c <= '9'; }

std::string canonical(std::string value) {
    const auto first = value.find_first_not_of('0');
    return first == std::string::npos ? "0" : value.substr(first);
}

std::pair<std::string, std::size_t> decimal(std::string_view text) {
    if (text.empty() || text.size() > 64)
        throw std::invalid_argument("expected positive plain decimal (maximum 64 characters)");
    std::string digits;
    bool point = false;
    std::size_t scale = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.' && !point && i != 0 && i + 1 != text.size()) {
            point = true;
        } else if (digit(c)) {
            digits += c;
            if (point) ++scale;
        } else {
            throw std::invalid_argument("expected positive plain decimal");
        }
    }
    digits = canonical(digits);
    if (digits == "0") throw std::invalid_argument("decimal must be positive");
    return {digits, scale};
}

bool less(const std::string& a, const std::string& b) {
    return a.size() != b.size() ? a.size() < b.size() : a < b;
}

// a >= b, both canonical nonnegative decimal integers.
std::string subtract(std::string a, const std::string& b) {
    int borrow = 0;
    for (std::size_t offset = 0; offset < a.size(); ++offset) {
        const auto i = a.size() - 1 - offset;
        int value = a[i] - '0' - borrow;
        if (offset < b.size()) value -= b[b.size() - 1 - offset] - '0';
        borrow = value < 0 ? 1 : 0;
        a[i] = static_cast<char>('0' + value + 10 * borrow);
    }
    return canonical(a);
}

template<typename T>
T integer(std::string_view text, std::string_view field) {
    if (text.empty() || text.size() > 20)
        throw std::invalid_argument(std::string(field) + ": expected unsigned integer (maximum 20 digits)");
    T value = 0;
    for (char c : text) {
        if (!digit(c)) throw std::invalid_argument(std::string(field) + ": expected unsigned integer");
        const auto d = static_cast<T>(c - '0');
        if (value > (std::numeric_limits<T>::max() - d) / 10)
            throw std::invalid_argument(std::string(field) + ": integer overflow");
        value = static_cast<T>(value * 10 + d);
    }
    return value;
}

// CSV quoting is decoded before field validation. Embedded commas/quotes or
// newlines cannot occur in any valid field in this schema.
std::array<std::string, 7> fields(std::string_view line) {
    std::array<std::string, 7> result;
    std::size_t pos = 0;
    for (std::size_t column = 0; column < result.size(); ++column) {
        auto& out = result[column];
        if (pos < line.size() && line[pos] == '"') {
            ++pos;
            bool closed = false;
            while (pos < line.size()) {
                const char c = line[pos++];
                if (c != '"') out += c;
                else if (pos < line.size() && line[pos] == '"') { out += '"'; ++pos; }
                else { closed = true; break; }
            }
            if (!closed) throw std::invalid_argument("unterminated quoted field");
        } else {
            while (pos < line.size() && line[pos] != ',') {
                if (line[pos] == '"') throw std::invalid_argument("quote inside unquoted field");
                out += line[pos++];
            }
        }
        if (column + 1 == result.size()) {
            if (pos != line.size()) throw std::invalid_argument("extra column or text after quote");
        } else {
            if (pos == line.size() || line[pos++] != ',')
                throw std::invalid_argument("missing column or text after quote");
        }
    }
    return result;
}
} // namespace

CsvError::CsvError(std::size_t line, const std::string& detail)
    : std::runtime_error("line " + std::to_string(line) + ": " + detail), line_(line) {}

DecimalTickSize::DecimalTickSize(std::string_view text) {
    auto [digits, scale] = decimal(text);
    digits_ = std::move(digits);
    scale_ = scale;
}

Price DecimalTickSize::to_ticks(std::string_view text) const {
    auto [numerator, scale] = decimal(text);
    auto denominator = digits_;
    // price/tick = (price_digits * 10^tick_scale) /
    //              (tick_digits * 10^price_scale).
    if (scale_ >= scale) numerator.append(scale_ - scale, '0');
    else denominator.append(scale - scale_, '0');
    std::string remainder = "0";
    Price quotient = 0;
    for (char c : numerator) {
        remainder = canonical(remainder + c);
        int q = 0;
        while (!less(remainder, denominator)) {
            remainder = subtract(remainder, denominator);
            ++q; // At most nine subtractions per long-division digit.
        }
        if (quotient > (std::numeric_limits<Price>::max() - q) / 10)
            throw std::invalid_argument("price ticks overflow int64");
        quotient = quotient * 10 + q;
    }
    if (remainder != "0" || quotient == 0)
        throw std::invalid_argument("price must be exactly tick aligned and positive");
    return quotient;
}

bool NormalizedCsvReader::read_line(std::string& text) {
    text.clear();
    ++line_;
    try {
        char c;
        while (input_.get(c)) {
            if (c == '\n') return true;
            if (c == '\r') {
                if (input_.peek() == '\n') input_.get();
                return true;
            }
            // All valid rows are much shorter; bound even malformed input.
            if (text.size() == 1024) throw CsvError(line_, "record exceeds 1024 bytes");
            text += c;
        }
    } catch (const std::ios_base::failure&) {
        if (!input_.eof() || input_.bad()) throw CsvError(line_, "stream read failure");
    }
    if (input_.bad() || !input_.eof()) throw CsvError(line_, "stream read failure");
    if (!text.empty()) return true;
    --line_;
    return false;
}

NormalizedCsvReader::NormalizedCsvReader(std::istream& input, const DecimalTickSize& tick_size)
    : input_(input), tick_size_(tick_size) {
    std::string text;
    if (!read_line(text)) throw CsvError(1, "missing header");
    try {
        const auto row = fields(text);
        for (std::size_t i = 0; i < header.size(); ++i)
            if (row[i] != header[i]) throw std::invalid_argument("header must match normalized schema exactly");
    } catch (const std::invalid_argument& error) { throw CsvError(line_, error.what()); }
}

std::optional<BookUpdate> NormalizedCsvReader::next() {
    std::string text;
    while (read_line(text)) {
        if (text.empty()) continue; // Preserve the Python CSV blank-line contract.
        try {
            const auto row = fields(text);
            const auto time = integer<Timestamp>(row[0], header[0]);
            const auto instrument = integer<InstrumentId>(row[1], header[1]);
            const auto sequence = integer<SequenceNumber>(row[2], header[2]);
            const auto price = tick_size_.to_ticks(row[3]);
            const auto quantity = integer<Quantity>(row[4], header[4]);
            if (row[5] != "BID" && row[5] != "ASK") throw std::invalid_argument("side must be BID or ASK");
            if (row[6] != "ADD" && row[6] != "MODIFY" && row[6] != "DELETE")
                throw std::invalid_argument("update_type must be ADD, MODIFY or DELETE");
            const auto type = row[6] == "ADD" ? UpdateType::Add : row[6] == "MODIFY" ? UpdateType::Modify : UpdateType::Delete;
            if (type == UpdateType::Delete ? quantity != 0 : quantity == 0)
                throw std::invalid_argument("quantity: ADD/MODIFY require positive, DELETE requires zero");
            return BookUpdate{time, std::nullopt, instrument, row[5] == "BID" ? Side::Bid : Side::Ask,
                              price, quantity, type, sequence};
        } catch (const std::invalid_argument& error) { throw CsvError(line_, error.what()); }
    }
    return std::nullopt;
}
} // namespace trading
