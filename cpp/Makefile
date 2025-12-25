# Makefile for prefix_match C++ implementation

CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -flto -fopenmp -Wall -Wextra
CXXFLAGS_DEBUG = -std=c++17 -g -O0 -fopenmp -Wall -Wextra -Wpedantic
LDFLAGS = -lz -fopenmp

# Default target
all: prefix_match

# Release build
prefix_match: main.cpp pattern_trie.cpp pattern_trie.hpp
	$(CXX) $(CXXFLAGS) -o $@ main.cpp pattern_trie.cpp $(LDFLAGS)

# Debug build
debug: main.cpp pattern_trie.cpp pattern_trie.hpp
	$(CXX) $(CXXFLAGS_DEBUG) -o prefix_match_debug main.cpp pattern_trie.cpp $(LDFLAGS)

# Static library
libpattern_trie.a: pattern_trie.cpp pattern_trie.hpp
	$(CXX) $(CXXFLAGS) -c pattern_trie.cpp -o pattern_trie.o
	ar rcs $@ pattern_trie.o
	rm -f pattern_trie.o

# Clean
clean:
	rm -f prefix_match prefix_match_debug libpattern_trie.a *.o

# Test with sample files
test: prefix_match
	./prefix_match -p ../patterns.txt -s ../strings.txt -m

.PHONY: all debug clean test
