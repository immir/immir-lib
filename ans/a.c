#if 0
set -x; gcc -Wall -Wextra -o a a.c -lm && ./a; exit
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <x86intrin.h>
#include <math.h>
#include <assert.h>

static uint64_t _maskr(int b) { return b == 64 ? ~0UL : (1UL<<b)-1; }

int main (int argc, char **argv) {

  while (--argc) putenv(*++argv);

  const int64_t S = atol(getenv("S") ?: "195");
  const int64_t b = atol(getenv("b") ?: "16");
  const int64_t M = atol(getenv("M") ?: "4096");
  const int64_t seed = getenv("seed") ? atol(getenv("seed")) : (int64_t) __rdtsc();
  const int64_t trials = atol(getenv("T") ?: "1");

  const uint64_t B = 1UL << b;
  const uint64_t L = B/S*S;
  const uint64_t H = L*B/S;
  const int64_t  n = (M*8-b) / log2(S);

  printf("S = %ld, b = %ld, n = %ld, L = %08lx, H = %08lx\n", S, b, n, L, H);

  int64_t data_len = M*8/b;
  uint64_t data[data_len];

  srand48(seed);

  int in[n], out[n];
  int bits = 0, n_words = 0, w = 0;
  uint64_t x_max = 0;

  int64_t count = trials;
  while (count-- > 0) {
    
    // ----------------------------------------------------------------------
    // data

    for (int i = 0; i < n; i++)
      in[i] = lrand48() % S;

    // ----------------------------------------------------------------------
    // encode
    
    bits = n_words = w = 0;
    uint64_t x = L;

    for (int i = 0; i < n; i++) {
      uint64_t s = in[n-1-i];
      if (x >= H) 
        data[w++] = x & _maskr(b), bits += b, x >>= b;
      x = x * S + s;
      if (x > x_max) x_max = x;
    }

    data[w++] = x & _maskr(b);
    data[w++] = x >> b;
    assert(x <= _maskr(2*b));
    bits += b+b;
    n_words = w;


    // ----------------------------------------------------------------------
    // decode

    w = n_words;
    x = data[--w];
    x <<= b;
    x += data[--w];

    for (int i = 0; i < n; i++) {
      assert(x >= L);
      uint64_t q = x / S;
      uint64_t s = x - q * S; assert(s == x % S);
      out[i] = s;
      x = q;
      if (x < L) x = (x << b) + data[--w];
    }

    //assert(x == L);

    // ----------------------------------------------------------------------
    // check

    int bad = 0;
    for (int i = 0; i < n; i++)
      bad += in[i] != out[i];

    if (bad) {
      printf("*** %d bad decodes of %ld\n", bad, n);
      exit(1);
    }
    
    putchar('.');
    fflush(stdout);
  }

  putchar('\n');

  double est = n * log2(S);

  printf("bits = %d\n", bits);
  printf("opt  = %.2f\n", est);
  printf("eff  = %.6f\n", est/bits);

  return 0;
}

