CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -march=native -DNDEBUG
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN = Aether-C

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

debug:
	$(CXX) -std=c++20 -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -march=native $(SRCS) -o $(BIN) $(LDFLAGS)

.PHONY: all clean debug
