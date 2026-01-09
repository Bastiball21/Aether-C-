#include "eval_tune.h"
#include "eval.h"
#include "eval_params.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <cstring> // for std::memset

namespace Eval {

    // Helper to parse result from EPD
    // Returns 1.0 (White Win), 0.0 (Black Win), 0.5 (Draw).
    float parse_result(const std::string& line) {
        // "1-0" or c9 "1-0"
        if (line.find("1-0") != std::string::npos) return 1.0f;
        if (line.find("0-1") != std::string::npos) return 0.0f;
        if (line.find("1/2-1/2") != std::string::npos) return 0.5f;
        return -1.0f; // Unknown
    }

    struct FeatureMap {
        std::vector<int> values;
        std::vector<std::string> names;
    };

    // Unused helper, removed to fix warning
    // void add_feature(...) {}

    struct FeatureSet {
        int mat[2][6]; // [side][pt]
        int bishop_pair[2];
        int rook_open[2];
        int rook_semi[2];
        int pawn_supported[2];
        int pawn_rank[2][8];
        int pawn_connected[2];
        int pawn_blocker[2];

        int bad_bishop[2];
        int rook_7th[2];
        int rook_behind[2];
        int knight_outpost[2];

        int tropism[2][8];
        int king_zone[2][6];
        int king_safety[2][100];

        int king_open[2];
        int king_semi[2];

        int restricted[2][6]; // P..K
        int restricted_strict[2][6];
        int pressure[2][6];
        int inactive[2];

        int pawn_iso[2];
        int pawn_double[2];

        int pst[2][6][64];
    };

    FeatureSet extract(const Position& pos) {
        FeatureSet fs;
        std::memset(&fs, 0, sizeof(FeatureSet));

        PawnEntry pawn_entry = evaluate_pawns(pos);
        Bitboard occ = pos.pieces();

        for (Color c : {WHITE, BLACK}) {
            Bitboard pawns = pos.pieces(PAWN, c);
            Bitboard original_pawns = pawns;

            while (pawns) {
                Square s = (Square)Bitboards::pop_lsb(pawns);
                File f = file_of(s);

                int p_idx = (c == WHITE) ? (s ^ 56) : s;
                fs.pst[c][PAWN][p_idx]++;
                fs.mat[c][PAWN]++;

                Bitboard file_mask = (Bitboards::FileA << f);
                Bitboard adj_mask = 0;
                if (f > FILE_A) adj_mask |= (Bitboards::FileA << (f - 1));
                if (f < FILE_H) adj_mask |= (Bitboards::FileA << (f + 1));

                if ((original_pawns & adj_mask) == 0) {
                    fs.pawn_iso[c]++;
                }
                if (Bitboards::more_than_one(original_pawns & file_mask)) {
                    fs.pawn_double[c]++;
                }
            }

            Bitboard passed = pawn_entry.passed_pawns[c];
            while (passed) {
                Square sq = (Square)Bitboards::pop_lsb(passed);
                int rank = rank_of(sq);
                fs.pawn_rank[c][rank]++;

                if (Bitboards::check_bit(pawn_entry.pawn_attacks[c], sq)) {
                    int bonus_rank_idx = (c == WHITE) ? rank : 7 - rank;
                    if (bonus_rank_idx >= 3) fs.pawn_supported[c]++;
                }
            }

            Bitboard passed_bb = pawn_entry.passed_pawns[c];
            Bitboard east = (passed_bb << 1) & ~Bitboards::FileA;
            Bitboard west = (passed_bb >> 1) & ~Bitboards::FileH;
            Bitboard connected = passed_bb & (east | west);
            fs.pawn_connected[c] += Bitboards::count(connected);

            Bitboard blocked_passed = pawn_entry.passed_front_mask[c] & occ;
            fs.pawn_blocker[c] += Bitboards::count(blocked_passed);
        }

        Square king_sqs[2];
        Bitboard king_rings[2];
        for (Color c : {WHITE, BLACK}) {
            king_sqs[c] = (Square)Bitboards::lsb(pos.pieces(KING, c));
            king_rings[c] = Bitboards::get_king_attacks(king_sqs[c]);

            int k_idx = (c == WHITE) ? (king_sqs[c] ^ 56) : king_sqs[c];
            fs.pst[c][KING][k_idx]++;
        }

        Bitboard attacks_by_side[2] = {0, 0};
        int king_attackers_count[2] = {0, 0};
        int king_attack_units[2] = {0, 0};
        Bitboard restricted_pieces[2] = {0, 0};

        for (Color us : {WHITE, BLACK}) {
            Color them = ~us;
            Bitboard my_bishops = pos.pieces(BISHOP, us);
            if (Bitboards::count(my_bishops) >= 2) fs.bishop_pair[us]++;

            Bitboard my_pawns = pos.pieces(PAWN, us);
            Bitboard enemy_pawns = pos.pieces(PAWN, them);

            for (int pt = 0; pt < 6; pt++) {
                if (pt == PAWN) continue;

                Bitboard bb = pos.pieces((PieceType)pt, us);
                while (bb) {
                    Square sq = (Square)Bitboards::pop_lsb(bb);

                    int pst_idx = (us == WHITE) ? (sq ^ 56) : sq;
                    fs.pst[us][pt][pst_idx]++;
                    fs.mat[us][pt]++;

                    Bitboard attacks = 0;
                    if (pt == KNIGHT) attacks = Bitboards::get_knight_attacks(sq);
                    else if (pt == BISHOP) attacks = Bitboards::get_bishop_attacks(sq, occ);
                    else if (pt == ROOK) attacks = Bitboards::get_rook_attacks(sq, occ);
                    else if (pt == QUEEN) attacks = Bitboards::get_queen_attacks(sq, occ);
                    else if (pt == KING) attacks = Bitboards::get_king_attacks(sq);

                    attacks_by_side[us] |= attacks;

                    if (pt == KING) continue;

                    Bitboard safe_mob = attacks & ~pos.pieces(us);
                    int mob_cnt = Bitboards::count(safe_mob);

                    Bitboard pawn_safe_attacks = safe_mob & ~pawn_entry.pawn_attacks[them];
                    int safe_mob_val = Bitboards::count(pawn_safe_attacks);

                    if (safe_mob_val <= 3) {
                        if (safe_mob_val <= 1) fs.restricted_strict[us][pt]++;
                        else fs.restricted[us][pt]++;
                    }
                    if (safe_mob_val <= 2) Bitboards::set_bit(restricted_pieces[us], sq);

                    if ((pt == KNIGHT || pt == BISHOP) && mob_cnt <= 2) fs.inactive[us]++;

                    if (pt == BISHOP) {
                         Bitboard light_sq_mask = 0x55AA55AA55AA55AAULL;
                         bool bishop_is_light = (Bitboards::check_bit(light_sq_mask, sq));
                         Bitboard my_pawns_same_color = my_pawns & (bishop_is_light ? light_sq_mask : ~light_sq_mask);
                         if (Bitboards::count(my_pawns_same_color) >= 3) fs.bad_bishop[us]++;
                    }

                    if (pt == KNIGHT) {
                         int r = rank_of(sq);
                         int rel_r = (us == WHITE) ? r : 7 - r;
                         if (rel_r >= 3 && rel_r <= 5) {
                              if (Bitboards::check_bit(pawn_entry.pawn_attacks[us], sq)) {
                                  fs.knight_outpost[us]++;
                              }
                         }
                    }

                    if (pt == ROOK) {
                        int f = file_of(sq);
                        int r = rank_of(sq);
                        Bitboard file_mask = (Bitboards::FileA << f);
                        bool my_p = (my_pawns & file_mask);
                        bool en_p = (enemy_pawns & file_mask);
                        if (!my_p) {
                            if (!en_p) fs.rook_open[us]++;
                            else fs.rook_semi[us]++;
                        }

                        int rel_r = (us == WHITE) ? r : 7 - r;
                        if (rel_r == 6) fs.rook_7th[us]++;

                        Bitboard my_passed_file = (pawn_entry.passed_pawns[us] & file_mask);
                        if (my_passed_file) {
                            Square relevant_pawn = (Square)Bitboards::lsb(my_passed_file);
                            if ((us == WHITE && sq < relevant_pawn) || (us == BLACK && sq > relevant_pawn)) {
                                fs.rook_behind[us]++;
                            }
                        }
                    }

                    int k_file = file_of(king_sqs[them]);
                    int k_rank = rank_of(king_sqs[them]);
                    int s_file = file_of(sq);
                    int s_rank = rank_of(sq);
                    int dist = std::max(std::abs(k_file - s_file), std::abs(k_rank - s_rank));
                    fs.tropism[us][dist]++;

                    Bitboard att_on_ring = attacks & king_rings[them];
                    if (att_on_ring) {
                        king_attack_units[them] += Params.KING_ZONE_ATTACK_WEIGHTS[pt] * Bitboards::count(att_on_ring);
                        king_attackers_count[them]++;
                        fs.king_zone[them][pt] += Bitboards::count(att_on_ring);
                    }
                }
            }
        }

        for (Color us : {WHITE, BLACK}) {
            Color them = ~us;
            Bitboard targets = restricted_pieces[them];
            while (targets) {
                Square sq = (Square)Bitboards::pop_lsb(targets);
                if (Bitboards::check_bit(attacks_by_side[us], sq)) {
                    if (!Bitboards::check_bit(pawn_entry.pawn_attacks[them], sq)) {
                        PieceType pt = (PieceType)(pos.piece_on(sq) % 6);
                        if (pt != NO_PIECE_TYPE && pt != KING && pt != PAWN) {
                             fs.pressure[us][pt]++;
                        }
                    }
                }
            }
        }

        for (Color side : {WHITE, BLACK}) {
            Square k_sq = king_sqs[side];
            Bitboard my_pawns = pos.pieces(PAWN, side);
            Bitboard enemy_pawns = pos.pieces(PAWN, ~side);
            File k_file = file_of(k_sq);
            for (int f_offset = -1; f_offset <= 1; f_offset++) {
                int f = k_file + f_offset;
                if (f >= 0 && f <= 7) {
                    Bitboard mask = (Bitboards::FileA << f);
                    bool friendly_pawn = (my_pawns & mask);
                    bool enemy_pawn = (enemy_pawns & mask);
                    if (!friendly_pawn) {
                        fs.king_semi[side]++;
                        if (!enemy_pawn) fs.king_open[side]++;
                    }
                }
            }

            if (king_attackers_count[side] >= 2) {
                int units = king_attack_units[side];
                if (units > 99) units = 99;
                fs.king_safety[side][units]++;
            }
        }

        return fs;
    }

    void tune_epd(const std::string& epd_file, const std::string& csv_file) {
        std::ifstream infile(epd_file);
        std::ofstream outfile(csv_file);

        if (!infile.is_open() || !outfile.is_open()) {
            std::cerr << "Error opening files.\n";
            return;
        }

        // Header
        outfile << "label,stm,phase,eval_stm";

        std::vector<std::string> features;
        auto add_n = [&](std::string name) {
            features.push_back(name + "_mg");
            features.push_back(name + "_eg");
        };

        const char* pt_names[] = {"p","n","b","r","q","k"};

        for(int p=0; p<5; p++) add_n(std::string("mat_") + pt_names[p]);
        for(int p=0; p<6; p++) {
            for(int s=0; s<64; s++) {
                add_n(std::string("pst_") + pt_names[p] + "_" + std::to_string(s));
            }
        }

        add_n("bishop_pair");
        add_n("rook_open");
        add_n("rook_semi");
        add_n("pawn_supported");
        for(int r=0; r<8; r++) add_n("pawn_rank_" + std::to_string(r));
        add_n("pawn_connected");
        add_n("pawn_blocker");
        add_n("pawn_iso");
        add_n("pawn_double");
        add_n("bad_bishop");
        add_n("rook_7th");
        add_n("rook_behind");
        add_n("knight_outpost");
        add_n("inactive");

        for(int p=1; p<5; p++) add_n(std::string("rest_") + pt_names[p]);
        for(int p=1; p<5; p++) add_n(std::string("rest_strict_") + pt_names[p]);
        for(int p=1; p<5; p++) add_n(std::string("pressure_") + pt_names[p]);

        for(int d=0; d<8; d++) add_n("tropism_" + std::to_string(d));

        add_n("king_open");
        add_n("king_semi");

        for(int i=0; i<100; i++) add_n("ks_" + std::to_string(i));

        for (const auto& f : features) outfile << "," << f;
        outfile << "\n";

        std::string line;
        while (std::getline(infile, line)) {
            std::stringstream ss(line);
            std::string token;
            std::vector<std::string> tokens;
            while(ss >> token) tokens.push_back(token);

            float result = parse_result(line);
            if (result < 0) continue;

            if (tokens.size() < 4) continue;
            std::string fen = tokens[0] + " " + tokens[1] + " " + tokens[2] + " " + tokens[3];
            int next_idx = 4;
            if (next_idx < (int)tokens.size() && isdigit(tokens[next_idx][0])) {
                fen += " " + tokens[next_idx];
                next_idx++;
                if (next_idx < (int)tokens.size() && isdigit(tokens[next_idx][0])) {
                    fen += " " + tokens[next_idx];
                    next_idx++;
                }
            } else {
                fen += " 0 1";
            }

            Position pos;
            pos.set(fen);

            FeatureSet fs = extract(pos);

            int eval = Eval::evaluate(pos);

            int phase = 0;
            for (int pt = 0; pt < 6; pt++) {
                phase += Bitboards::count(pos.pieces((PieceType)pt)) * Params.PHASE_WEIGHTS[pt];
            }
            phase = std::clamp(phase, 0, 24);
            double p = (double)phase / 24.0;

            // Convert result to Side-To-Move perspective
            // parse_result returns White perspective result.
            // If STM is Black, result_stm = 1.0 - result.
            float result_stm = result;
            if (pos.side_to_move() == BLACK) {
                result_stm = 1.0f - result;
            }

            // Output label, stm, phase, eval_stm
            outfile << result_stm << "," << (pos.side_to_move() == WHITE ? 0 : 1) << "," << phase << "," << eval;

            auto out_feat = [&](int white_val, int black_val) {
                int net = (pos.side_to_move() == WHITE) ? (white_val - black_val) : (black_val - white_val);
                outfile << "," << (net * p) << "," << (net * (1.0 - p));
            };

            for(int pt=0; pt<5; pt++) out_feat(fs.mat[WHITE][pt], fs.mat[BLACK][pt]);
            for(int pt=0; pt<6; pt++) {
                for(int s=0; s<64; s++) out_feat(fs.pst[WHITE][pt][s], fs.pst[BLACK][pt][s]);
            }

            out_feat(fs.bishop_pair[WHITE], fs.bishop_pair[BLACK]);
            out_feat(fs.rook_open[WHITE], fs.rook_open[BLACK]);
            out_feat(fs.rook_semi[WHITE], fs.rook_semi[BLACK]);
            out_feat(fs.pawn_supported[WHITE], fs.pawn_supported[BLACK]);
            for(int r=0; r<8; r++) out_feat(fs.pawn_rank[WHITE][r], fs.pawn_rank[BLACK][r]);
            out_feat(fs.pawn_connected[WHITE], fs.pawn_connected[BLACK]);
            out_feat(fs.pawn_blocker[WHITE], fs.pawn_blocker[BLACK]);
            out_feat(fs.pawn_iso[WHITE], fs.pawn_iso[BLACK]);
            out_feat(fs.pawn_double[WHITE], fs.pawn_double[BLACK]);
            out_feat(fs.bad_bishop[WHITE], fs.bad_bishop[BLACK]);
            out_feat(fs.rook_7th[WHITE], fs.rook_7th[BLACK]);
            out_feat(fs.rook_behind[WHITE], fs.rook_behind[BLACK]);
            out_feat(fs.knight_outpost[WHITE], fs.knight_outpost[BLACK]);
            out_feat(fs.inactive[WHITE], fs.inactive[BLACK]);

            for(int pt=1; pt<5; pt++) out_feat(fs.restricted[WHITE][pt], fs.restricted[BLACK][pt]);
            for(int pt=1; pt<5; pt++) out_feat(fs.restricted_strict[WHITE][pt], fs.restricted_strict[BLACK][pt]);
            for(int pt=1; pt<5; pt++) out_feat(fs.pressure[WHITE][pt], fs.pressure[BLACK][pt]);

            for(int d=0; d<8; d++) out_feat(fs.tropism[WHITE][d], fs.tropism[BLACK][d]);

            out_feat(fs.king_open[WHITE], fs.king_open[BLACK]);
            out_feat(fs.king_semi[WHITE], fs.king_semi[BLACK]);

            for(int i=0; i<100; i++) out_feat(fs.king_safety[WHITE][i], fs.king_safety[BLACK][i]);

            outfile << "\n";
        }

        std::cout << "EPD tuning dump complete.\n";
    }
}
