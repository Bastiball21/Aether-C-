#include "syzygy.h"
#include "syzygy/tbprobe.h"
#include <iostream>
#include <vector>
#include <sstream>

namespace Syzygy {

    bool is_inited = false;

    void set_path(const std::string& path) {
        if (path.empty()) {
            is_inited = false;
            // Optionally free Fathom resources if needed, but Fathom doesn't expose tb_free easily.
            // Just marking it disabled is enough for engine logic.
            return;
        }
        bool success = tb_init(path.c_str());
        if (success && TB_LARGEST > 0) {
            is_inited = true;
            std::cout << "info string Syzygy found " << TB_LARGEST << "-man TBs" << std::endl;
        } else {
            is_inited = false; // Ensure it stays disabled if init failed or empty TBs
            std::cout << "info string Syzygy initialization failed or no TBs found" << std::endl;
        }
    }

    bool enabled() {
        return is_inited;
    }

    bool probe_root(const Position& pos, uint16_t& best_move, int& score) {
        if (!is_inited) return false;

        if (pos.castling_rights_mask() != 0) return false;
        if ((unsigned)Bitboards::count(pos.pieces()) > TB_LARGEST) return false;
        // if (pos.rule50_count() > 0) return false; // Not strictly required for root probing if we handle DTZ

        unsigned results = tb_probe_root(
            pos.pieces(WHITE), pos.pieces(BLACK),
            pos.pieces(KING), pos.pieces(QUEEN), pos.pieces(ROOK), pos.pieces(BISHOP), pos.pieces(KNIGHT), pos.pieces(PAWN),
            pos.rule50_count(),
            pos.castling_rights_mask(),
            pos.en_passant_square() == SQ_NONE ? 0 : pos.en_passant_square(),
            pos.side_to_move() == WHITE,
            nullptr
        );

        if (results == TB_RESULT_FAILED) return false;

        unsigned from = TB_GET_FROM(results);
        unsigned to = TB_GET_TO(results);
        unsigned promo = TB_GET_PROMOTES(results);

        int flag = 0;

        // Map Fathom promo to Aether flags
        // Aether flags for promo: 8 | type. type: 0=N, 1=B, 2=R, 3=Q
        if (promo != TB_PROMOTES_NONE) {
             if (promo == TB_PROMOTES_KNIGHT) flag = 8 | 0;
             if (promo == TB_PROMOTES_BISHOP) flag = 8 | 1;
             if (promo == TB_PROMOTES_ROOK)   flag = 8 | 2;
             if (promo == TB_PROMOTES_QUEEN)  flag = 8 | 3;
        }

        best_move = (from << 6) | to;
        if (flag != 0) best_move |= (flag << 12);

        // We need score. probe_root returns a result containing WDL and DTZ.
        unsigned wdl = TB_GET_WDL(results);

        int wdl_score = 0;
        if (wdl == TB_WIN) wdl_score = 20000;
        else if (wdl == TB_LOSS) wdl_score = -20000;
        else if (wdl == TB_CURSED_WIN) wdl_score = 5000; // Cursed wins are less valuable
        else if (wdl == TB_BLESSED_LOSS) wdl_score = -5000;
        else wdl_score = 0;

        score = wdl_score;
        return true;
    }

    bool probe_wdl(const Position& pos, int& score, int ply) {
        if (!is_inited) return false;

        if (pos.castling_rights_mask() != 0) return false;
        if ((unsigned)Bitboards::count(pos.pieces()) > TB_LARGEST) return false;
        if (pos.rule50_count() >= 100) return false;

        unsigned wdl = tb_probe_wdl(
            pos.pieces(WHITE), pos.pieces(BLACK),
            pos.pieces(KING), pos.pieces(QUEEN), pos.pieces(ROOK), pos.pieces(BISHOP), pos.pieces(KNIGHT), pos.pieces(PAWN),
            pos.rule50_count(),
            pos.castling_rights_mask(),
            pos.en_passant_square() == SQ_NONE ? 0 : pos.en_passant_square(),
            pos.side_to_move() == WHITE
        );

        if (wdl == TB_RESULT_FAILED) return false;

        int val = 0;
        if (wdl == TB_WIN) val = 20000 - ply;
        else if (wdl == TB_LOSS) val = -20000 + ply;
        else if (wdl == TB_CURSED_WIN) val = 5000 - ply;
        else if (wdl == TB_BLESSED_LOSS) val = -5000 + ply;
        else val = 0;

        score = val;
        return true;
    }

}
