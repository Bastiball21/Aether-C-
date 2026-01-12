# Aether-C

A C++20 chess engine ported from a Rust engine, using Hand Crafted Evaluation (HCE).

## Build

Requirements:
- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 19.29+)
- Make

To build the engine:
```bash
make
```

To build a debug version:
```bash
make debug
```

## UCI Commands

The engine supports the Universal Chess Interface (UCI) protocol.

### Example Session

```
uci
isready
position startpos
go wtime 60000 btime 60000
stop
quit
```

### Supported Options

- `Hash`: Transposition table size in MB (default 64).
- `Threads`: Number of threads (currently only 1 supported).
- `MoveOverhead`: Time buffer in milliseconds (default 10).
- `UCI_Chess960`: Enable Chess960 mode (currently not fully implemented).

## Debugging

### Perft

You can run a performance test (move generation correctness) using the `perft` command.

```
position startpos
perft 5
```

Or `divide` to see node counts per move:

```
position startpos
divide 4
```

### Known Perft Values (Start Position)

- Depth 1: 20
- Depth 2: 400
- Depth 3: 8902
- Depth 4: 197281
- Depth 5: 4865609
- Depth 6: 119060324

## Datagen (self-play + PGN conversion)

Datagen lives in the C++ codebase and can be invoked by linking against
`src/datagen.cpp` and calling the API functions in `src/datagen.h`.

### API overview

```cpp
DatagenConfig cfg;
cfg.num_games = 10000;
cfg.num_threads = 4;
cfg.output_path = "train.bin";
cfg.seed = 1234;
cfg.opening_book_path = "tools/opening_book.epd";
cfg.opening_random_plies = 8;
cfg.chess960 = false;

run_datagen(cfg);
```

`run_datagen` outputs a binary stream with repeating records:

```
float result;
int16 score_white;
uint16 fen_len;
byte[fen_len] fen;
```

The generator uses engine evals for score labels and applies mercy/stable-win/draw
adjudication along with opening-skip and sampling rules.
Note: `chess960` is a reserved flag for future support; current datagen uses
the standard start position only.

Optional flags:
- `--book <path>`: EPD opening book to seed starting positions.
- `--random-plies <n>`: random opening plies applied after the seed position.
- `--nodes <n>`: fixed node budget per move (takes priority when > 0).
- `--depth <n>`: fixed depth for move selection when nodes is not set.

### Datagen command

```bash
./Aether-C.exe datagen 10000 4 train.bin --book tools/opening_book.epd --seed 1234 --random-plies 8 --nodes 50000
```

### PGN conversion

```cpp
convert_pgn("games.pgn", "train.bin");
```

`convert_pgn` expects PGN-like input where the move tokens are UCI moves
(e.g., `e2e4`, `g1f3`). SAN parsing is not implemented yet.

Sample opening books live in `tools/opening_book.epd` and `tools/opening_book.pgn`.

### Training

Use the Bullet training pipeline with the generated binary data:
https://github.com/jw1912/bullet
