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
    int sample_top_n = 4;
    int sample_top_k = 4;
    int temp_schedule_plies = 40;
    double temp_start = 1.0;
    double temp_end = 0.6;
    double epsilon = 0.1;
    bool use_epsilon_greedy = false;
};

void run_datagen(const DatagenConfig& config);
void convert_pgn(const std::string& pgn_path, const std::string& output_path);

#endif // DATAGEN_H
