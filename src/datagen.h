#ifndef DATAGEN_H
#define DATAGEN_H

#include "packed_board_io.h"
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
    double search_nodes_jitter = 0.2;
    int search_depth = 1;
    int sample_top_n = 4;
    int sample_top_k = 4;
    int temp_schedule_plies = 40;
    double temp_start = 1.0;
    double temp_end = 0.6;
    double epsilon = 0.1;
    bool use_epsilon_greedy = false;
    int min_depth = 0;
    int64_t min_nodes = 0;
    int record_every = 1;
    int balance_equal_cp = 200;
    int balance_moderate_cp = 600;
    int balance_equal_keep = 100;
    int balance_moderate_keep = 50;
    int balance_extreme_keep = 25;
    size_t record_lru_size = 8192;
    size_t writer_lru_size = 0;
    PackedFormat output_format = PackedFormat::V2;
    bool adjudicate = true;
};

void run_datagen(const DatagenConfig& config);
void convert_pgn(const std::string& pgn_path, const std::string& output_path,
    PackedFormat format = PackedFormat::V2);

#endif // DATAGEN_H
