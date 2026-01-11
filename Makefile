CXX = g++
CC = gcc

# FLAGS EXPLANATION:
# -std=c++20: Uses C++20 standard
# -O3: Maximum optimization level
# -march=native: Optimizes specifically for i5-8250U (enables AVX2/BMI2)
# -flto: Link Time Optimization (makes the engine much faster)
# -DNDEBUG: Disables debugging asserts for speed
# -static: bundles system libraries so the .exe runs standalone on Windows
# -I src: Include src directory for headers
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -march=native -flto -DNDEBUG -static -I src
CFLAGS = -O3 -Wall -Wextra -march=native -flto -DNDEBUG -static -I src -std=gnu99

LDFLAGS = -pthread

SRC_DIR = src
EVAL_DIR = src/eval
SYZYGY_DIR = src/syzygy
OBJ_DIR = obj
BIN = Aether-C.exe

SRCS_CXX = $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(EVAL_DIR)/*.cpp)
# Only compile tbprobe.c, tbcore.c is included by tbprobe.c
SRCS_C = $(SYZYGY_DIR)/tbprobe.c

OBJS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(notdir $(SRCS_CXX))) \
       $(patsubst %.c, $(OBJ_DIR)/%.o, $(notdir $(SRCS_C)))

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(EVAL_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SYZYGY_DIR)/%.c | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

debug: CXXFLAGS = -std=c++20 -O0 -g -Wall -Wextra -march=native -I src
debug: CFLAGS = -O0 -g -Wall -Wextra -march=native -I src -std=gnu99
debug: $(BIN)

.PHONY: all clean debug
