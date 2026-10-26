#include "replay/historical_replay.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        std::string snapshot_path, updates_path, tick_text;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 == argc) throw std::invalid_argument("missing option value");
            const std::string option = argv[i];
            std::string* destination = nullptr;
            if (option == "--snapshot") destination = &snapshot_path;
            else if (option == "--updates") destination = &updates_path;
            else if (option == "--tick-size") destination = &tick_text;
            else throw std::invalid_argument("unknown option: " + option);
            if (!destination->empty()) throw std::invalid_argument("duplicate option: " + option);
            if (std::string{argv[i + 1]}.empty())
                throw std::invalid_argument("empty option value: " + option);
            *destination = argv[i + 1];
        }
        if (snapshot_path.empty() || updates_path.empty() || tick_text.empty())
            throw std::invalid_argument("usage: historical_replay --snapshot FILE --updates FILE --tick-size DECIMAL");
        const trading::DecimalTickSize tick_size{tick_text};
        std::ifstream snapshot{snapshot_path, std::ios::binary};
        std::ifstream updates{updates_path, std::ios::binary};
        if (!snapshot) throw std::runtime_error("cannot open snapshot: " + snapshot_path);
        if (!updates) throw std::runtime_error("cannot open updates: " + updates_path);
        trading::HistoricalReplay replay;
        const auto summary = replay.run(snapshot, updates, tick_size);
        const auto& book = *replay.book();
        std::cout << "instrument=" << summary.instrument
                  << "\nsnapshot_events=" << summary.snapshot_events
                  << "\nupdate_events=" << summary.update_events
                  << "\nlast_sequence=" << *book.last_sequence()
                  << "\nbid_levels=" << book.bid_levels()
                  << "\nask_levels=" << book.ask_levels();
        const auto print_level = [](const char* side, const std::optional<trading::PriceLevel>& level) {
            std::cout << "\nbest_" << side << '=';
            if (level) std::cout << level->price;
            else std::cout << "none";
            std::cout << "\nbest_" << side << "_quantity=" << (level ? level->quantity : 0);
        };
        print_level("bid", book.best_bid());
        print_level("ask", book.best_ask());
        std::cout << "\nstate=VALID\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "historical replay failed: " << error.what() << '\n';
        return 1;
    }
}
