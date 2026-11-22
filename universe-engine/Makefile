CXX = g++
# ULTRA: no wasted cycle - SoA, branchless, prefetch, restrict, inline, huge pages, PGO-ready
CXXFLAGS = -O3 -march=native -mtune=native -mavx2 -mfma -mavxvnni -flto -fwhole-program -DNDEBUG -std=c++17 -Wall -Wextra -pthread -ffast-math -funroll-loops -fomit-frame-pointer -fno-stack-protector -fprefetch-loop-arrays -fstrict-aliasing -falign-functions=32 -falign-loops=32 -pipe -I./src -DUSE_AVX2 -DUSE_BRANCHLESS -DUSE_PREFETCH
LDFLAGS = -pthread -flto -fwhole-program -Wl,-O3 -Wl,--gc-sections
# PGO: make pgo-generate && ./universe_engine --gen-synthetic 10 --iterations 50000 && make pgo-use

SRC = src/main.cpp src/indicators.cpp src/strategy.cpp src/optimizer.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = universe_engine

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $(TARGET) with AVX2 + O3 + flto"
	@ls -lh $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) *.gcda *.gcno
	rm -rf results/*.csv results/*.json

# Branchless + SoA + prefetch already in src/data.hpp:20 src/avx_math.hpp:10 src/indicators.cpp:15
check:
	./$(TARGET) --gen-synthetic 5 --iterations 2000 --threads 4 --top 10

benchmark:
	taskset -c 0-11 nice -n -20 ./$(TARGET) --gen-synthetic 10 --iterations 20000 --threads 0

huge:
	taskset -c 0-11 nice -n -20 ./$(TARGET) --data ./data --iterations 1000000 --threads 0 --top 200

pgo-generate:
	$(CXX) -O3 -march=native -mavx2 -mfma -flto -fprofile-generate -DNDEBUG -std=c++17 -ffast-math -pthread -I./src -o $(TARGET)_pgo src/main.cpp src/indicators.cpp src/strategy.cpp src/optimizer.cpp -pthread -flto

pgo-use:
	$(CXX) -O3 -march=native -mavx2 -mfma -flto -fprofile-use -DNDEBUG -std=c++17 -ffast-math -pthread -I./src -o $(TARGET) src/main.cpp src/indicators.cpp src/strategy.cpp src/optimizer.cpp -pthread -flto

.PHONY: all clean check benchmark huge pgo-generate pgo-use
