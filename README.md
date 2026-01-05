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
