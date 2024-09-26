
CC = $(CXX)
CXXFLAGS := -std=c++23 -march=native -O3 -g -Wall -Wextra -Werror -fopenmp
LDFLAGS := -fopenmp

default: s test

s.o : s.hpp
*.o : makefile

.phony: test
test: s
	./s B=90
