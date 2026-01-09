# Aether-C Development Roadmap

This file documents the current state of evaluation and future improvement ideas.

## Current Evaluation Features
- **Pawn Structure**: Isolated, Doubled, Passed (Rank Bonus, Supported, Connected, Blocker Penalty). Cached in `PawnHash`.
- **Material**: Standard MG/EG values + Phase blending.
- **Mobility**: Pseudo-legal mobility with safety checks (Restricted/Inactive penalties).
- **King Safety**: Attack Unit model (Attackers count + Weighted sum of attacks into King Zone -> Safety Table Lookup) + File Safety (Open/Semi-open files).
- **Other**: Bishop Pair, Rook on Open Files, King Tropism.

## Future Evaluation Improvements

### 1. Solidify Eval Structure
- **Tempo Bonus**: Add a small Score for `side_to_move` (mostly MG) to avoid undervaluing the initiative.
- **Tapered Consistency**: Ensure every new term uses (MG, EG) pairs and blends correctly via `Phase`.
- **Clamping**: Prevent massive evaluation swings by clamping individual term contributions where appropriate.

### 2. Advanced Pawn Evaluation
- **Pawn Majority**: Bonus for having more pawns on one wing (especially in EG).
- **Candidate Passed Pawns**: Bonus for pawns that can become passed after a forceable exchange/push.

### 3. Piece Activity & Strategy
- **Mobility Refinement**: Tune mobility weights and safe square definitions.
- **Rook Specials**:
  - Rook on 7th (Rank 7 for White, Rank 2 for Black) bonus (especially if cutting off King).
  - Rook behind passed pawn (very strong EG term).
- **Knight Outposts**: Bonus for Knights on squares protected by pawns that cannot be attacked by enemy pawns.
- **Bad Bishop**: Penalty for Bishops blocked by friendly pawns on the same color complex.

### 4. Endgame Knowledge
- **King Centralization**: Bonus for King proximity to center/pawns in EG.
- **Opposite Colored Bishops (OCB)**: Scale down evaluation score if OCB logic detects drawish tendencies.
- **Pawnless Endings**: Known drawish patterns (e.g., KRP vs KR).

### 5. Performance
- **Incremental Evaluation**: Update evaluation terms (Material, PST) incrementally in `StateInfo` during `make_move`/`unmake_move` instead of recomputing from scratch.
