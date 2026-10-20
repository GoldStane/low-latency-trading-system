#pragma once

#include <cstdint>

namespace trading {
using Price = std::int64_t;           // Integer ticks; conversion belongs to adapters.
using Quantity = std::uint32_t;      // Absolute quantity at one price level.
using InstrumentId = std::uint32_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::int64_t;      // Nanoseconds since Unix epoch, never latency.
} // namespace trading
