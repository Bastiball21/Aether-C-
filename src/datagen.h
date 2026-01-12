#ifndef DATAGEN_H
#define DATAGEN_H

#include <cstdint>
#include <string>

struct DatagenConfig {
    int64_t num_games = 0;
    int num_threads = 1;
    std::string output_path;
    uint64_t seed = 0;
    bool chess960 = false;
    std::string opening_book_path;
    int opening_random_plies = 8;
    int64_t search_nodes = 0;
    int search_depth = 1;
};

void run_datagen(const DatagenConfig& config);
void convert_pgn(const std::string& pgn_path, const std::string& output_path);

#endif // DATAGEN_H
