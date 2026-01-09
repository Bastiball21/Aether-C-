# Aether-C Development Roadmap

This file documents the current state of evaluation and future improvement ideas.

## Current Evaluation Features
- **Pawn Structure**: Isolated, Doubled, Passed (Rank Bonus, Supported, Connected, Blocker Penalty). Cached in `PawnHash`.
- **Material**: Standard MG/EG values + Phase blending.
- **Mobility**: Pseudo-legal mobility with safety checks (Restricted/Inactive penalties).
- **King Safety**: Attack Unit model (Attackers count + Weighted sum of attacks into King Zone -> Safety Table Lookup) + File Safety (Open/Semi-open files).
- **Piece Activity**:
    - **Bishop**: Pair bonus, Bad Bishop penalty (blocked by own pawns).
    - **Rook**: Open/Semi-open file bonus, On 7th Rank bonus, Behind Passed Pawn bonus.
    - **Knight**: Outpost bonus (Rank 4-6, supported by pawn).
- **Endgame**:
    - **King Activity**: Centrality bonus in EG.
    - **Scaling**: OCB (Opposite Colored Bishops) scaling to reduce evaluation in drawish positions.
- **Other**: King Tropism.

## Future Evaluation Improvements

### 1. Solidify Eval Structure
- **Tempo Bonus**: Add a small Score for `side_to_move` (mostly MG) to avoid undervaluing the initiative.
- **Tapered Consistency**: Ensure every new term uses (MG, EG) pairs and blends correctly via `Phase`.
- **Clamping**: Prevent massive evaluation swings by clamping individual term contributions where appropriate.

### 2. Advanced Pawn Evaluation
- **Pawn Majority**: Bonus for having more pawns on one wing (especially in EG).
- **Candidate Passed Pawns**: Bonus for pawns that can become passed after a forceable exchange/push.

### 3. Piece Activity & Strategy Refinements
- **Mobility Refinement**: Tune mobility weights and safe square definitions further.
- **Trapped Pieces**: Penalty for pieces trapped (e.g., Bishop trapped A2/H2, Rook trapped by King).

### 4. Endgame Knowledge Refinements
- **Pawnless Endings**: Known drawish patterns (e.g., KRP vs KR).
- **Fortress Detection**: Detect specific fortress patterns where material advantage cannot win.

### 5. Performance
- **Incremental Evaluation**: Update evaluation terms (Material, PST) incrementally in `StateInfo` during `make_move`/`unmake_move` instead of recomputing from scratch.
