#include "eval_params.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>

namespace Eval {

    EvalParams Params;

    #define A(...) { __VA_ARGS__ }

    void init_params() {
        // Defaults
        int mg_vals[] = { 100, 320, 330, 500, 900, 0 };
        std::memcpy(Params.MG_VALS, mg_vals, sizeof(mg_vals));
        int eg_vals[] = { 100, 320, 330, 500, 900, 0 };
        std::memcpy(Params.EG_VALS, eg_vals, sizeof(eg_vals));
        int phase[] = { 0, 1, 1, 2, 4, 0 };
        std::memcpy(Params.PHASE_WEIGHTS, phase, sizeof(phase));

        int kt[] = { 10, 8, 5, 2, 0, 0, 0, 0 };
        std::memcpy(Params.KING_TROPISM_PENALTY, kt, sizeof(kt));

        Params.SHIELD_MISSING_PENALTY = -40;
        Params.SHIELD_OPEN_FILE_PENALTY = -50;

        Params.BISHOP_PAIR_BONUS_MG = 30;
        Params.BISHOP_PAIR_BONUS_EG = 50;
        Params.ROOK_OPEN_FILE_BONUS_MG = 30;
        Params.ROOK_OPEN_FILE_BONUS_EG = 15;
        Params.ROOK_SEMI_OPEN_FILE_BONUS_MG = 15;
        Params.ROOK_SEMI_OPEN_FILE_BONUS_EG = 10;
        Params.PASSED_PAWN_SUPPORTED_BONUS_MG = 10;
        Params.PASSED_PAWN_SUPPORTED_BONUS_EG = 20;

        int pp_rank_mg[] = { 0, 5, 10, 20, 35, 60, 100, 0 };
        std::memcpy(Params.PASSED_PAWN_RANK_BONUS_MG, pp_rank_mg, sizeof(pp_rank_mg));
        int pp_rank_eg[] = { 0, 10, 20, 40, 60, 100, 150, 0 };
        std::memcpy(Params.PASSED_PAWN_RANK_BONUS_EG, pp_rank_eg, sizeof(pp_rank_eg));

        Params.PASSED_PAWN_CONNECTED_BONUS_MG = 10;
        Params.PASSED_PAWN_CONNECTED_BONUS_EG = 20;
        Params.PASSED_PAWN_BLOCKER_PENALTY_MG = -20;
        Params.PASSED_PAWN_BLOCKER_PENALTY_EG = -40;

        Params.TEMPO_BONUS = 20;

        Params.BAD_BISHOP_PENALTY_MG = -10;
        Params.BAD_BISHOP_PENALTY_EG = -10;
        Params.ROOK_ON_SEVENTH_MG = 20;
        Params.ROOK_ON_SEVENTH_EG = 40;
        Params.ROOK_BEHIND_PASSED_MG = 10;
        Params.ROOK_BEHIND_PASSED_EG = 30;
        Params.KNIGHT_OUTPOST_BONUS_MG = 25;
        Params.KNIGHT_OUTPOST_BONUS_EG = 15;

        int kza[] = { 0, 0, 2, 2, 3, 5 };
        std::memcpy(Params.KING_ZONE_ATTACK_WEIGHTS, kza, sizeof(kza));

        int ks_table[] = {
            0,  0,  1,  2,  3,  5,  7,  9, 12, 15,
           18, 22, 26, 30, 35, 39, 44, 49, 54, 60,
           66, 72, 78, 84, 91, 98,105,112,120,128,
          136,144,153,162,171,180,190,200,210,220,
          231,242,253,264,276,288,300,313,326,339,
          353,367,381,396,411,426,442,458,474,491,
          508,526,544,562,581,600,620,640,661,682,
          704,726,749,772,796,820,845,870,896,922,
          949,977,1000,1000,1000,1000,1000,1000,1000,1000
        };
        std::memcpy(Params.KING_SAFETY_TABLE, ks_table, sizeof(ks_table));

        Params.KING_OPEN_FILE_PENALTY = 20;
        Params.KING_SEMI_OPEN_FILE_PENALTY = 10;

        int rest_mg[] = { 0, 20, 20, 12, 8, 0 };
        std::memcpy(Params.RESTRICTED_PENALTY_MG, rest_mg, sizeof(rest_mg));
        int rest_eg[] = { 0, 10, 10, 6, 4, 0 };
        std::memcpy(Params.RESTRICTED_PENALTY_EG, rest_eg, sizeof(rest_eg));

        int strict_mg[] = { 0, 40, 40, 24, 16, 0 };
        std::memcpy(Params.RESTRICTED_STRICT_PENALTY_MG, strict_mg, sizeof(strict_mg));
        int strict_eg[] = { 0, 20, 20, 12, 8, 0 };
        std::memcpy(Params.RESTRICTED_STRICT_PENALTY_EG, strict_eg, sizeof(strict_eg));

        int press_mg[] = { 0, 10, 10, 6, 4, 0 };
        std::memcpy(Params.PRESSURE_BONUS_MG, press_mg, sizeof(press_mg));
        int press_eg[] = { 0, 10, 10, 6, 4, 0 };
        std::memcpy(Params.PRESSURE_BONUS_EG, press_eg, sizeof(press_eg));

        Params.INACTIVE_PENALTY_MG = 15;
        Params.INACTIVE_PENALTY_EG = 15;

        Params.PAWN_ISOLATED_MG = 10;
        Params.PAWN_ISOLATED_EG = 10;
        Params.PAWN_DOUBLED_MG = 15;
        Params.PAWN_DOUBLED_EG = 15;

        // PSTs
        int pst_pawn_mg[64] = A(
            0,   0,   0,   0,   0,   0,   0,   0,
            50,  50,  50,  50,  50,  50,  50,  50,
            10,  10,  20,  30,  30,  20,  10,  10,
             5,   5,  10,  25,  25,  10,   5,   5,
             0,   0,  20,  50,  50,  20,   0,   0,
             5,   5,  10,  20,  20,  10,   5,   5,
             0,   0,   0, -10, -10,   0,   0,   0,
             0,   0,   0,   0,   0,   0,   0,   0
        );
        std::memcpy(Params.MG_PAWN_TABLE, pst_pawn_mg, sizeof(pst_pawn_mg));

        int pst_pawn_eg[64] = A( 0, 0, 0, 0, 0, 0, 0, 0, 139, 140, 135, 114, 130, 114, 147, 164, 56, 61, 53, 35, 36, 21, 55, 60, 19, 5, 4, -10, -10, 10, -3, 19, 25, 10, 14, 24, 19, 11, 21, 21, 14, 20, 12, 31, 28, 20, 21, 20, 20, 16, 2, 18, 16, 2, 2, 14, 0, 0, 0, 0, 0, 0, 0, 0 );
        std::memcpy(Params.EG_PAWN_TABLE, pst_pawn_eg, sizeof(pst_pawn_eg));

        int pst_knight_mg[64] = A( -168, -89, -34, -49, 59, -97, -14, -108, -71, -42, 67, 32, 20, 57, 6, -17, -47, 55, 27, 52, 75, 122, 69, 43, -8, 3, 8, 28, 4, 59, 3, 19, -7, 1, 1, 0, 9, 1, 19, -7, -18, -2, -12, -1, 12, -15, 24, -11, -28, -52, -9, 0, 3, 12, -13, -19, -103, 13, -52, -28, -11, -22, 5, -22 );
        std::memcpy(Params.MG_KNIGHT_TABLE, pst_knight_mg, sizeof(pst_knight_mg));
        int pst_knight_eg[64] = A( -57, -38, -15, -29, -33, -28, -60, -99, -24, -11, -29, -9, -12, -29, -24, -50, -26, -25, -4, -7, -12, -20, -23, -41, -14, -4, 1, -2, 0, -3, 0, -18, -18, -10, -1, 7, -1, 5, 0, -17, -22, -6, -10, 1, 3, -13, -18, -21, -41, -21, -8, -7, -4, -21, -22, -41, -27, -45, -18, -13, -19, -11, -42, -61 );
        std::memcpy(Params.EG_KNIGHT_TABLE, pst_knight_eg, sizeof(pst_knight_eg));

        int pst_bishop_mg[64] = A( -32, 2, -82, -37, -24, -43, 6, -10, -24, 11, -19, -15, 28, 55, 13, -45, -18, 33, 35, 33, 30, 46, 33, -6, -3, 4, 13, 37, 22, 29, 6, -2, -6, 9, -1, 12, 19, -7, 6, 2, -2, 10, 4, -16, 0, 11, 7, 5, 3, -9, 7, 20, -3, 15, -7, 0, -30, -1, 39, -17, -8, 33, -39, -22 );
        std::memcpy(Params.MG_BISHOP_TABLE, pst_bishop_mg, sizeof(pst_bishop_mg));
        int pst_bishop_eg[64] = A( -16, -21, -11, -10, -5, -10, -16, -25, -8, -8, 1, -13, -5, -17, -4, -12, 0, -15, -11, -8, -10, 0, -4, 1, -6, -2, 0, -8, -3, -1, 1, 0, -11, -3, -2, -2, -8, -1, -5, -10, -14, -9, -6, -11, -1, -20, -11, -14, -16, -7, -9, -4, -11, -11, -22, -21, -12, -18, 1, -2, -2, -3, -9, -20 );
        std::memcpy(Params.EG_BISHOP_TABLE, pst_bishop_eg, sizeof(pst_bishop_eg));

        int pst_rook_mg[64] = A( 25, 36, 26, 44, 58, 6, 29, 39, 18, 20, 47, 52, 71, 61, 20, 40, -9, 13, 21, 28, 11, 41, 57, 15, -23, -11, 1, 20, 19, 32, -9, -20, -37, -26, -16, -4, 6, -7, 4, -27, -44, -26, -18, -17, -2, 0, -6, -35, -44, -18, -22, -14, -2, 9, -4, -71, -1, -12, -14, -11, -12, -3, -25, 0 );
        std::memcpy(Params.MG_ROOK_TABLE, pst_rook_mg, sizeof(pst_rook_mg));
        int pst_rook_eg[64] = A( -6, -5, 2, -1, 0, 3, 2, -3, -11, -12, -14, -12, -22, -13, -5, -6, -4, -6, -7, -12, -8, -12, -12, -6, 2, -1, -2, -9, -8, -4, -4, 1, 0, 0, 0, -4, -8, -9, -11, -11, -6, -3, -11, -3, -12, -16, -11, -16, -9, -13, -7, -5, -12, -13, -12, -3, -13, -2, -7, -20, -20, -10, 5, -13 );
        std::memcpy(Params.EG_ROOK_TABLE, pst_rook_eg, sizeof(pst_rook_eg));

        int pst_queen_mg[64] = A( -30, -2, 26, 10, 56, 42, 41, 41, -21, -39, -5, -1, -17, 53, 24, 52, -12, -17, 3, 5, 24, 52, 41, 54, -25, -25, -18, -20, -4, 13, -3, 1, -9, -28, -10, -10, -7, -5, 0, -5, -11, 2, -13, -3, -7, 0, 10, 6, -30, -6, 7, 1, 0, 13, 0, 2, 2, -9, -2, 29, -8, -17, -28, -48 );
        std::memcpy(Params.MG_QUEEN_TABLE, pst_queen_mg, sizeof(pst_queen_mg));
        int pst_queen_eg[64] = A( -9, 19, 18, 24, 24, 16, 8, 17, -15, 19, 31, 38, 56, 22, 27, 0, -17, 6, 6, 46, 42, 32, 15, 7, 4, 22, 21, 41, 37, 37, 34, 13, -17, 26, 16, 41, 27, 32, 38, 22, -14, -26, 12, 3, 5, 15, 9, 5, -19, -23, -30, -15, -16, -24, -34, -31, -30, -24, -19, -37, -2, -27, -18, -40 );
        std::memcpy(Params.EG_QUEEN_TABLE, pst_queen_eg, sizeof(pst_queen_eg));

        int pst_king_mg[64] = A( -64, 22, 15, -15, -56, -34, 1, 12, 28, -1, -20, -8, -8, -4, -38, -29, -9, 23, 0, -17, -21, 4, 19, -22, -16, -21, -13, -28, -32, -28, -17, -36, -47, 0, -27, -41, -49, -48, -35, -51, -12, -11, -21, -46, -46, -32, -18, -24, 3, 8, -4, -55, -33, -19, -2, 0, -10, 29, 11, -34, 26, -7, -9, 29 );
        std::memcpy(Params.MG_KING_TABLE, pst_king_mg, sizeof(pst_king_mg));
        int pst_king_eg[64] = A( -73, -36, -19, -20, -12, 12, 2, -18, -12, 15, 10, 11, 12, 33, 18, 7, 9, 13, 16, 8, 13, 36, 31, 11, -6, 17, 15, 16, 14, 17, 12, 2, -12, 0, 18, 15, 8, 2, -2, -11, -11, 9, 15, 23, 9, 7, -3, -4, -20, 0, 13, 19, 10, 4, -7, -8, -45, -25, -8, 20, 2, 6, -4, -7 );
        std::memcpy(Params.EG_KING_TABLE, pst_king_eg, sizeof(pst_king_eg));
    }

    bool load_params(const char* filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::cout << "Loading weights from " << filename << "...\n";

        // Map feature name to EvalParams update function
        // This is tedious to map manually but required.
        // I will implement a parser that reads "key": value and updates the map.
        // But since I need to update struct members, I need a giant if/else or map of pointers.

        std::string line;
        while (std::getline(file, line)) {
            // Find "key": value
            size_t q1 = line.find('"');
            if (q1 == std::string::npos) continue;
            size_t q2 = line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;

            std::string key = line.substr(q1 + 1, q2 - q1 - 1);

            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;

            // Value could be after colon, maybe comma at end
            std::string val_str = line.substr(colon + 1);
            // Remove comma if any
            size_t comma = val_str.find(',');
            if (comma != std::string::npos) val_str = val_str.substr(0, comma);

            int val = 0;
            try {
                val = std::stoi(val_str);
            } catch(...) { continue; }

            // Apply value
            // We need to parse key. Format: name_mg or name_eg.
            // If name ends with _mg, it's MG.

            bool is_mg = (key.length() > 3 && key.substr(key.length()-3) == "_mg");
            bool is_eg = (key.length() > 3 && key.substr(key.length()-3) == "_eg");

            if (!is_mg && !is_eg) continue;

            std::string base = key.substr(0, key.length() - 3);

            // Helper Macros
            #define SET(name, target) if (base == name) { if(is_mg) Params.target##_MG = val; else Params.target##_EG = val; continue; }
            #define SET_ARR(name, target, idx) if (base == name + "_" + std::to_string(idx)) { if(is_mg) Params.target##_MG[idx] = val; else Params.target##_EG[idx] = val; continue; }

            // Material
            const char* pt_names[] = {"p","n","b","r","q","k"};
            for(int p=0; p<6; p++) {
                if (base == std::string("mat_") + pt_names[p]) {
                    if(is_mg) Params.MG_VALS[p] = val; else Params.EG_VALS[p] = val;
                }
            }

            // PST
            for(int p=0; p<6; p++) {
                // pst_p_0_mg
                // base is pst_p_0
                std::string prefix = std::string("pst_") + pt_names[p] + "_";
                if (base.find(prefix) == 0) {
                    int sq = std::stoi(base.substr(prefix.length()));
                    if (sq >=0 && sq < 64) {
                        int* table = nullptr;
                        if (p==0) table = is_mg ? Params.MG_PAWN_TABLE : Params.EG_PAWN_TABLE;
                        else if (p==1) table = is_mg ? Params.MG_KNIGHT_TABLE : Params.EG_KNIGHT_TABLE;
                        else if (p==2) table = is_mg ? Params.MG_BISHOP_TABLE : Params.EG_BISHOP_TABLE;
                        else if (p==3) table = is_mg ? Params.MG_ROOK_TABLE : Params.EG_ROOK_TABLE;
                        else if (p==4) table = is_mg ? Params.MG_QUEEN_TABLE : Params.EG_QUEEN_TABLE;
                        else if (p==5) table = is_mg ? Params.MG_KING_TABLE : Params.EG_KING_TABLE;

                        if (table) table[sq] = val;
                    }
                }
            }

            SET("bishop_pair", BISHOP_PAIR_BONUS);
            SET("rook_open", ROOK_OPEN_FILE_BONUS);
            SET("rook_semi", ROOK_SEMI_OPEN_FILE_BONUS);
            SET("pawn_supported", PASSED_PAWN_SUPPORTED_BONUS);
            for(int r=0; r<8; r++) {
                if (base == "pawn_rank_" + std::to_string(r)) {
                    if(is_mg) Params.PASSED_PAWN_RANK_BONUS_MG[r] = val; else Params.PASSED_PAWN_RANK_BONUS_EG[r] = val;
                }
            }
            SET("pawn_connected", PASSED_PAWN_CONNECTED_BONUS);
            SET("pawn_blocker", PASSED_PAWN_BLOCKER_PENALTY);
            SET("pawn_iso", PAWN_ISOLATED);
            SET("pawn_double", PAWN_DOUBLED);
            SET("bad_bishop", BAD_BISHOP_PENALTY);
            SET("rook_7th", ROOK_ON_SEVENTH);
            SET("rook_behind", ROOK_BEHIND_PASSED);
            SET("knight_outpost", KNIGHT_OUTPOST_BONUS);
            SET("inactive", INACTIVE_PENALTY);

            for(int p=1; p<5; p++) {
                if (base == std::string("rest_") + pt_names[p]) {
                    if(is_mg) Params.RESTRICTED_PENALTY_MG[p] = val; else Params.RESTRICTED_PENALTY_EG[p] = val;
                }
                if (base == std::string("rest_strict_") + pt_names[p]) {
                    if(is_mg) Params.RESTRICTED_STRICT_PENALTY_MG[p] = val; else Params.RESTRICTED_STRICT_PENALTY_EG[p] = val;
                }
                if (base == std::string("pressure_") + pt_names[p]) {
                    if(is_mg) Params.PRESSURE_BONUS_MG[p] = val; else Params.PRESSURE_BONUS_EG[p] = val;
                }
            }

            for(int d=0; d<8; d++) {
                if (base == "tropism_" + std::to_string(d)) {
                    if(is_mg) Params.KING_TROPISM_PENALTY[d] = val; else Params.KING_TROPISM_PENALTY[d] = (val); // User code had val/2 for EG? But we tune explicit EG weight.
                }
            }

            if (base == "king_open") { if (is_mg) Params.KING_OPEN_FILE_PENALTY = val; continue; }
            if (base == "king_semi") { if (is_mg) Params.KING_SEMI_OPEN_FILE_PENALTY = val; continue; }

            for(int i=0; i<100; i++) {
                if (base == "ks_" + std::to_string(i)) {
                    if(is_mg) Params.KING_SAFETY_TABLE[i] = val;
                    // EG safety? We used hardcoded /4.
                    // If we tune, we only tune MG safety table usually?
                    // Or we tune EG too?
                    // In `extract`, we output ks_N_mg and ks_N_eg.
                    // But `Params` only has `KING_SAFETY_TABLE` (one array).
                    // This implies we can only support one set of weights unless we split the struct.
                    // Since the struct has `KING_SAFETY_TABLE[100]`, and the eval logic uses it for MG (and scales for EG),
                    // we should probably only load MG values?
                    // Or average them?
                    // For now, I'll load into the table from MG values.
                }
            }
        }

        return true;
    }
}
