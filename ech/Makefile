CC ?= gcc
CFLAGS ?= -O3 -march=native -std=gnu11 -Wall -Wextra -Werror
LDLIBS = -lm

all: a

a: a.c gf2.h bitops.h
	$(CC) $(CFLAGS) a.c -o $@ $(LDLIBS)

gf2_test: tests/gf2_test.c a.c gf2.h bitops.h
	$(CC) $(CFLAGS) tests/gf2_test.c -o $@ $(LDLIBS)

.PHONY: all check clean
check: gf2_test
	./gf2_test

clean:
	rm -f a gf2_test
