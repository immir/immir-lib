CC ?= gcc
CFLAGS ?= -O3 -march=native -std=gnu11 -Wall -Wextra -Werror
LDLIBS = -lm

all: ech

ech: ech.c gf2.h bitops.h
	$(CC) $(CFLAGS) ech.c -o $@ $(LDLIBS)

gf2_test: tests/gf2_test.c ech.c gf2.h bitops.h
	$(CC) $(CFLAGS) tests/gf2_test.c -o $@ $(LDLIBS)

.PHONY: all check clean
check: gf2_test
	./gf2_test

clean:
	rm -f ech gf2_test
