CXX ?= g++
CXXFLAGS ?= -O3 -march=native -std=c++23 -fopenmp -Wall -Wextra -Werror

.PHONY: all clean
all: plethy

plethy: plethy.cpp beta_core.hpp
	$(CXX) $(CXXFLAGS) -o $@ plethy.cpp

clean:
	rm -f plethy
