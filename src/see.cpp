#include "see.h"
#include "bitboard.h"
#include "movegen.h" // For piece definitions if needed
#include <algorithm> // For std::max

// Piece values for SEE: P=100 N=320 B=330 R=500 Q=900 K=20000
// Note: These must match what's used in search or be reasonable for SEE
static const int piece_values[] = { 100, 320, 330, 500, 900, 20000 };

static int get_piece_value(Piece p) {
    if (p == NO_PIECE) return 0;
    return piece_values[p % 6];
}

// Helper to get attackers to a square
// Returns a bitboard of all pieces attacking 'sq' given 'occ'
static Bitboard attackers_to(Square sq, Bitboard occ, const Position& pos) {
    Bitboard attackers = 0;
    // Pawns
    // Attacks to 'sq' from WHITE pawns means squares that BLACK pawns would attack
    attackers |= (Bitboards::get_pawn_attacks(sq, BLACK) & pos.pieces(WHITE));
    attackers |= (Bitboards::get_pawn_attacks(sq, WHITE) & pos.pieces(BLACK));

    // Knights
    attackers |= (Bitboards::get_knight_attacks(sq) & (pos.pieces(KNIGHT, WHITE) | pos.pieces(KNIGHT, BLACK)));

    // Kings
    attackers |= (Bitboards::get_king_attacks(sq) & (pos.pieces(KING, WHITE) | pos.pieces(KING, BLACK)));

    // Sliders
    Bitboard queens = pos.pieces(QUEEN, WHITE) | pos.pieces(QUEEN, BLACK);
    Bitboard rooks = pos.pieces(ROOK, WHITE) | pos.pieces(ROOK, BLACK);
    Bitboard bishops = pos.pieces(BISHOP, WHITE) | pos.pieces(BISHOP, BLACK);

    Bitboard rook_attacks = Bitboards::get_rook_attacks(sq, occ);
    Bitboard bishop_attacks = Bitboards::get_bishop_attacks(sq, occ);

    attackers |= (rook_attacks & (rooks | queens));
    attackers |= (bishop_attacks & (bishops | queens));

    return attackers;
}

// Get the least valuable attacker from the bitboard 'attackers' for side 'stm'
// Removes the attacker from 'attackers' bitboard (passed by value so effectively just finding it)
// We return the square and the piece type.
// Actually we need to remove it from 'occ' later.
// We should check piece types in order: Pawn, Knight, Bishop, Rook, Queen, King.
static Square get_least_valuable_attacker(Bitboard attackers, Color stm, const Position& pos, int& piece_val) {
    Bitboard stm_attackers = attackers & pos.pieces(stm);
    if (!stm_attackers) return SQ_NONE;

    // Pawns
    Bitboard pawns = stm_attackers & pos.pieces(PAWN);
    if (pawns) {
        piece_val = piece_values[PAWN];
        return Bitboards::lsb(pawns);
    }
    // Knights
    Bitboard knights = stm_attackers & pos.pieces(KNIGHT);
    if (knights) {
        piece_val = piece_values[KNIGHT];
        return Bitboards::lsb(knights);
    }
    // Bishops
    Bitboard bishops = stm_attackers & pos.pieces(BISHOP);
    if (bishops) {
        piece_val = piece_values[BISHOP];
        return Bitboards::lsb(bishops);
    }
    // Rooks
    Bitboard rooks = stm_attackers & pos.pieces(ROOK);
    if (rooks) {
        piece_val = piece_values[ROOK];
        return Bitboards::lsb(rooks);
    }
    // Queens
    Bitboard queens = stm_attackers & pos.pieces(QUEEN);
    if (queens) {
        piece_val = piece_values[QUEEN];
        return Bitboards::lsb(queens);
    }
    // King
    Bitboard kings = stm_attackers & pos.pieces(KING);
    if (kings) {
        piece_val = piece_values[KING];
        return Bitboards::lsb(kings);
    }
    return SQ_NONE;
}

int see(const Position& pos, uint16_t move) {
    Square from = (Square)((move >> 6) & 0x3F);
    Square to = (Square)(move & 0x3F);
    int flag = (move >> 12);

    int gain[64]; // Increased buffer size
    int d = 0;

    Piece victim = pos.piece_on(to);
    int val_victim = get_piece_value(victim);
    if (flag == 5) { // EP
        val_victim = piece_values[PAWN];
    }

    // Initial capture
    int val_attacker = get_piece_value(pos.piece_on(from));

    // Promotion
    if (flag & 8) {
        int p = (flag & 3);
        static const int promo_vals[] = {320, 330, 500, 900};
        val_attacker = promo_vals[p];
        val_victim += val_attacker - piece_values[PAWN];
    }

    gain[d++] = val_victim;

    Bitboard occ = pos.pieces();
    Bitboard attackers = attackers_to(to, occ, pos);

    Bitboards::clear_bit(occ, from);
    attackers &= ~(1ULL << from); // Remove 'from'

    Bitboard rooks = pos.pieces(ROOK) | pos.pieces(QUEEN);
    Bitboard bishops = pos.pieces(BISHOP) | pos.pieces(QUEEN);

    // Add X-rays behind 'from'
    attackers |= (Bitboards::get_rook_attacks(to, occ) & rooks);
    attackers |= (Bitboards::get_bishop_attacks(to, occ) & bishops);

    int current_val = val_attacker;
    Color stm = ~pos.side_to_move();

    while (d < 63) { // Safety guard
        int att_val = 0;
        Square att_sq = get_least_valuable_attacker(attackers, stm, pos, att_val);
        if (att_sq == SQ_NONE) break;

        gain[d] = current_val - gain[d-1];

        if (std::max(-gain[d-1], gain[d]) < 0) break;

        d++;
        current_val = att_val;

        Bitboards::clear_bit(occ, att_sq);
        attackers &= ~(1ULL << att_sq);

        attackers |= (Bitboards::get_rook_attacks(to, occ) & rooks);
        attackers |= (Bitboards::get_bishop_attacks(to, occ) & bishops);

        stm = ~stm;
    }

    while (--d > 0) {
        if (-gain[d] < gain[d-1]) {
            gain[d-1] = -gain[d];
        }
    }

    return gain[0];
}
