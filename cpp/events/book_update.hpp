#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <optional>

namespace trading {
enum class Side : std::uint8_t { Bid, Ask };
enum class UpdateType : std::uint8_t { Add, Modify, Delete };
enum class MarketDataState : std::uint8_t { Empty, Syncing, Valid, Invalid };

struct BookUpdate {
    Timestamp exchange_time; // std::int64_t
    std::optional<Timestamp> receive_time; // std::int64_t
    InstrumentId instrument; // std::uint32_t
    Side side; // enum std::uint8_t
    Price price; // std::int64_t
    Quantity quantity; // std::uint32_t
    UpdateType type; // enum std::uint8_t
    SequenceNumber sequence; // std::uint64_t
};
} // namespace trading
