CXX = g++

# FLAGS EXPLANATION:
# -std=c++20: Uses C++20 standard
# -O3: Maximum optimization level
# -march=native: Optimizes specifically for i5-8250U (enables AVX2/BMI2)
# -flto: Link Time Optimization (makes the engine much faster)
# -DNDEBUG: Disables debugging asserts for speed
# -static: bundles system libraries so the .exe runs standalone on Windows
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -march=native -flto -DNDEBUG -static
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
# Added .exe for Windows
BIN = Aether-C.exe

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

# Debug build (Includes debug symbols, disables optimization)
debug:
	$(CXX) -std=c++20 -O0 -g -Wall -Wextra -march=native $(SRCS) -o $(BIN) $(LDFLAGS)

.PHONY: all clean debug
