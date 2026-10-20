#include "order_book/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    using namespace trading;
    using Clock = std::chrono::steady_clock;
    OrderBook book{1};
    book.begin_sync();
    SequenceNumber sequence = 0;
    if (book.apply({0, {}, 1, Side::Bid, 100, 1, UpdateType::Add, sequence++}) != BookError::None ||
        book.apply({0, {}, 1, Side::Ask, 102, 1, UpdateType::Add, sequence++}) != BookError::None ||
        book.finish_sync() != BookError::None) return 1;
    std::vector<double> samples;
    samples.reserve(1000);
    constexpr int batch_size = 300;
    for (int batch = 0; batch < 1100; ++batch) {
        const auto start = Clock::now();
        for (int i = 0; i < batch_size; ++i) {
            const auto type = i % 3 == 0 ? UpdateType::Add : i % 3 == 1 ? UpdateType::Modify : UpdateType::Delete;
            const Quantity quantity = type == UpdateType::Delete ? 0 : 5;
            if (book.apply({0, {}, 1, Side::Bid, 101, quantity, type, sequence++}) != BookError::None) return 1;
        }
        const auto ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
        if (batch >= 100) samples.push_back(ns / batch_size);
    }
    double total = 0;
    for (auto sample : samples) total += sample;
    std::sort(samples.begin(), samples.end());
    std::cout << "batch_mean_ns_per_event p50=" << samples[499]
              << " p95=" << samples[949] << " p99=" << samples[989]
              << " events_per_second=" << 1e9 / (total / static_cast<double>(samples.size())) << '\n';
}
