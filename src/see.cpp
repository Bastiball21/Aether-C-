#include "see.h"
#include "bitboard.h"
#include "movegen.h" // For attacker generation if needed, but we can use Bitboards directly

// Piece values for SEE: P=100 N=320 B=330 R=500 Q=900 K=20000
static const int piece_values[] = { 100, 320, 330, 500, 900, 20000 };

static int get_piece_value(Piece p) {
    if (p == NO_PIECE) return 0;
    return piece_values[p % 6];
}

// Get the least valuable attacker
// Returns the attacker square and updates 'occupancy' by removing the attacker.
// If no attacker, returns SQ_NONE.
static Square get_least_valuable_attacker(const Position& pos, Square target, Bitboard& occupancy, Color side) {
    // Check pawns
    // pawn_attacks(sq, side) returns attacks FROM side.
    // We want attackers OF target (which is occupied by ~side initially, but we need attackers belonging to 'side')
    // get_pawn_attacks(sq, side) -> squares that side's pawns at sq attack.
    // Wait, get_pawn_attacks takes (Square, Color) -> usually means "squares attacked by a pawn of Color at Square"
    // OR "squares that attack Square if they have a pawn of Color".
    // Looking at bitboard.h: get_pawn_attacks(Square sq, Color side);
    // Usually standard is: attacks_from_pawn(sq, side).
    // To find attackers OF 'target' BY 'side', we need pawns of 'side' that attack 'target'.
    // That corresponds to pawn_attacks(target, ~side) if the function is symmetric (attacks from A to B == attacks from B to A for pawns? No.)
    // Captures are diagonal.
    // If we have a white pawn at d4, it attacks c5/e5.
    // If we have a target at d4, checked by black pawns at c5/e5.
    // So to find white attackers of d5: we look for white pawns at c4/e4.
    // This is equivalent to "where a black pawn at d5 would attack".
    Bitboard pawns = pos.pieces(PAWN, side) & occupancy;
    Bitboard potential_attackers = Bitboards::get_pawn_attacks(target, ~side);
    Bitboard pawn_attackers = potential_attackers & pawns;

    if (pawn_attackers) {
        Square sq = (Square)Bitboards::lsb(pawn_attackers);
        occupancy &= ~(1ULL << sq);
        return sq;
    }

    // Knights
    Bitboard knights = pos.pieces(KNIGHT, side) & occupancy;
    Bitboard knight_attackers = Bitboards::get_knight_attacks(target) & knights;
    if (knight_attackers) {
        Square sq = (Square)Bitboards::lsb(knight_attackers);
        occupancy &= ~(1ULL << sq);
        return sq;
    }

    // Bishops
    Bitboard bishops = pos.pieces(BISHOP, side) & occupancy;
    Bitboard bishop_attackers = Bitboards::get_bishop_attacks(target, occupancy) & bishops;
    if (bishop_attackers) {
        Square sq = (Square)Bitboards::lsb(bishop_attackers);
        occupancy &= ~(1ULL << sq);
        return sq;
    }

    // Rooks
    Bitboard rooks = pos.pieces(ROOK, side) & occupancy;
    Bitboard rook_attackers = Bitboards::get_rook_attacks(target, occupancy) & rooks;
    if (rook_attackers) {
        Square sq = (Square)Bitboards::lsb(rook_attackers);
        occupancy &= ~(1ULL << sq);
        return sq;
    }

    // Queens
    Bitboard queens = pos.pieces(QUEEN, side) & occupancy;
    Bitboard queen_attackers = (Bitboards::get_bishop_attacks(target, occupancy) | Bitboards::get_rook_attacks(target, occupancy)) & queens;
    if (queen_attackers) {
        Square sq = (Square)Bitboards::lsb(queen_attackers);
        occupancy &= ~(1ULL << sq);
        return sq;
    }

    // King
    Bitboard kings = pos.pieces(KING, side) & occupancy;
    Bitboard king_attackers = Bitboards::get_king_attacks(target) & kings;
    if (king_attackers) {
        Square sq = (Square)Bitboards::lsb(king_attackers);
        occupancy &= ~(1ULL << sq);
        return sq;
    }

    return SQ_NONE;
}

int see(const Position& pos, uint16_t move) {
    Square from = (Square)((move >> 6) & 0x3F);
    Square to = (Square)(move & 0x3F);
    int flag = (move >> 12);

    int gain[32];
    int d = 0;

    Piece victim = pos.piece_on(to);

    // Initial gain is value of victim
    int value = get_piece_value(victim);

    // Handle en-passant
    if (flag == 5) { // EP flag
        value = piece_values[PAWN]; // We capture a pawn
    } else if ((flag & 8)) { // Promotion
        int p = (flag & 3); // 0=N, 1=B, 2=R, 3=Q
        static const int promo_vals[] = {320, 330, 500, 900};
        // Promotion adds value: new piece - pawn
        value += promo_vals[p] - piece_values[PAWN];
    }

    gain[d++] = value;

    // We don't need these lines, removing warnings
    // Piece attacker_piece = pos.piece_on(from);

    // Let's restart the standard algorithm logic
    // Occupancy starts with all pieces
    Bitboard occupancy = pos.pieces();

    // 1. Initial capture
    // The "piece on square" is initially the victim.
    // The first attacker is 'from'.
    // We "make" the move by removing 'from' and placing attacker on 'to'.

    int score = get_piece_value(victim);
    if (flag == 5) { // EP
         score = piece_values[PAWN];
    }

    // If promotion, the piece landing on 'to' is the promoted piece.
    // If not, it's the moving piece.
    int moving_piece_val = get_piece_value(pos.piece_on(from));
    if ((flag & 8)) {
        int p = (flag & 3);
        static const int promo_vals[] = {320, 330, 500, 900};
        moving_piece_val = promo_vals[p];
        // And we gained the difference (Promotion - Pawn) which is already accounted if we consider
        // the "cost" of the attacker to be the Pawn but the "value" on the square to be the Promoted Piece?
        // Actually, easiest way:
        // Initial Gain = Victim Value + (PromotedValue - PawnValue) if promotion.
        // Next attacker risks capturing the Promoted Piece.
        score += moving_piece_val - get_piece_value(pos.piece_on(from));
    }

    gain[0] = score;

    // Remove the 'from' piece from occupancy (it moved to 'to')
    occupancy &= ~(1ULL << from);

    // The piece currently on 'to' (liable to be captured next) has value `moving_piece_val`.
    int piece_on_sq_val = moving_piece_val;
    Color side = ~pos.side_to_move(); // Next side to capture

    d = 1;
    while (true) {
        Square att_sq = get_least_valuable_attacker(pos, to, occupancy, side);
        if (att_sq == SQ_NONE) break;

        // Value of the piece attacking (and landing on square)
        Piece att_piece = pos.piece_on(att_sq);
        int att_val = get_piece_value(att_piece);

        // gain[d] = value_of_piece_captured - gain[d-1]
        gain[d] = piece_on_sq_val - gain[d-1];

        piece_on_sq_val = att_val;
        side = ~side;
        d++;
    }

    // Propagate back
    while (--d > 0) {
        // If the side could choose to stand pat (not capture), they will if capturing is worse.
        // -gain[d-1] is the score for the previous side if they capture.
        // If they don't capture, score is 0 (relative to that state).
        // But gain array accumulates swaps.
        // gain[d] is score for side to move at depth d.
        // gain[d-1] = -max(-gain[d-1], gain[d])
        if (-gain[d] < gain[d-1]) {
            gain[d-1] = -gain[d];
        }
    }

    return gain[0];
}
