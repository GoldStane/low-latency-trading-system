#pragma once

#include "events/book_update.hpp"

#include <istream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace trading {
class CsvError : public std::runtime_error {
public:
    CsvError(std::size_t line, const std::string& detail);
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
private:
    std::size_t line_;
};

// Decimal strings are limited to 64 characters, matching the Python contract.
// Decimal digit arithmetic avoids overflow in intermediate scaled values.
class DecimalTickSize {
public:
    explicit DecimalTickSize(std::string_view text);
    [[nodiscard]] Price to_ticks(std::string_view price) const;
private:
    std::string digits_;
    std::size_t scale_;
};

// Borrows the stream; caller must keep it alive. No ordering/book validation.
// After an exception, discard the reader. next() returns nullopt only at EOF.
class NormalizedCsvReader {
public:
    NormalizedCsvReader(std::istream& input, const DecimalTickSize& tick_size);
    [[nodiscard]] std::optional<BookUpdate> next();
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
private:
    bool read_line(std::string& text);
    std::istream& input_;
    DecimalTickSize tick_size_;
    std::size_t line_{0};
};
} // namespace trading
