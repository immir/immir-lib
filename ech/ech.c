#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <math.h>
#include <assert.h>

#include <limits.h>

#include "bitops.h"

static unsigned long long rdtsc (void) {
  unsigned int lo, hi;
  __asm__ __volatile__("lfence\n\trdtsc\n\tlfence"
                       : "=a"(lo), "=d"(hi) : : "memory");
  return ((unsigned long long)hi << 32) | lo; }

// ----------------------------------------------------------------------
// --- static helper functions should be inlined

static uint64_t bits (const uint64_t *V, int i, int b) {
  int w0 = i/64, w1 = (i+b-1)/64, ii = i%64;
  uint64_t x = V[w0] >> ii;
  if (w0 < w1)
    x ^= V[w1] << (64 - ii);
  x &= (b < 64) ? (1UL<<b)-1 : ~0UL;   // 1UL<<64 would be UB; assumes b<=64
  return x;
}

static uint64_t bit (const uint64_t *V, int i) {
  uint64_t probe = 1UL << (i%64);
  return (V[i/64] & probe) != 0;
}

static void addrows (uint64_t *A, const uint64_t *B, const uint64_t *C, int s, int wds) {
  for (int w = s; w < wds; w++)
    A[w] = B[w] ^ C[w];
}

static uint64_t dotprod (const uint64_t *A, const uint64_t *B, int w0, int w1) {
  uint64_t x = 0;
  for (int w = w0; w < w1; w++)
    x ^= A[w] & B[w];
  return stdc_count_ones_ull(x) & 1;
}

// ----------------------------------------------------------------------
// main worker functions

static int semi_ech(const int m, const int n, const int wds, uint64_t (*A)[wds],
                    int S, int *piv) {

  // 4 Russians with S bit tables; Z[] holds the precomputed row sums,
  // z[] the indexing into Z[] required since we don't reduce above the
  // block diagonal...

  assert(m > 0 && n > 0 && wds > 0);
  assert((size_t)wds >= ((size_t)n + 63)/64);
  if (S == 0) S = log(m); // reasonable default
  assert((unsigned)S < sizeof(int)*CHAR_BIT - 1);

  const int SS = (1<<S);
  assert((size_t)SS <= SIZE_MAX / sizeof(uint64_t) / wds);
  // could skip mallocs here if S < 2
  uint64_t (*Z)[wds] = malloc(SS * sizeof *Z); // Z[SS][wds]
  int *z = malloc(SS * sizeof *z);
  assert(Z);
  assert(z);

  int r = 0; // row in reduction
  int s = 0; // block start for 4 Russians table

  if (piv) for (int r=0; r<m; r++) piv[r] = -1;

  /*
     This diagram summarises the method. We use "4 Russian" tables
     of width S bits (S=3 below); current block starts at column s;
     to find pivot for row r, we start at j=r and reduce to the
     left using pivot rows t .. r-1 and their columns in cols[];
     then check for a pivot in column c -- if found, (possibly)
     xor it onto row r. Once the block is complete, form its table and
     reduce below...

         +-----+--------------------------------+
         |1 * *|* * * * * * * * * * * * * * * * |
         |  1 *|* * * * * * * * * * * * * * * * |
         |    1|* * * * * * * * * * * * * * * * |
         +-----+-----+--------------------------+
     t-> |     |1 * *|* * * * * * * * * * * * * |
         |     |0 0 *|* * * * * * * * * * * * * | <- r
         |     |0 0 *|* * * * * * * * * * * * * |
         |     |* * *|* * * * * * * * * * * * * | <- j
         |     |* * *|* * * * * * * * * * * * * |
         |     |* * *|* * * * * * * * * * * * * |
         +--------------------------------------+

      Missing pivots skip a column; a block with p pivots needs
      only 2^p table entries. A simpler loop handles the last columns.

   */

  for (; S > 1 && s + S <= n && r < m;) {

    // S columns at a time for 4 Russians method
    const int t = r; // first pivot row in this block
    int cols[S];

    int c = s;
    for (; c < s + S && r < m; c++) {

      // a long search clears a partial block before continuing
      const int end = r > t && m-r > 16 ? r+16 : m;
      int j;
      for (j = r; j < end; j++) {
        for (int k = t; k < r; k++)
          // reduce relative to this block using pivots found so far
          if (bit(A[j], cols[k-t]))
            addrows (A[j], A[j], A[k], s/64, wds);
        // now check for new pivot
        if (bit(A[j], c)) break;
      }

      if (j == end && end < m) break;
      if  (j == m)
        // no pivot in this column, try the next
        continue;

      if (j != r)
        // xor onto row r to get pivot there (if it's not already)
        addrows (A[r], A[r], A[j], s/64, wds);

      cols[r-t] = c;
      if (piv) piv[r] = c;
      r++;
    }

    // no rows below us, or no new pivots in this block
    if (r == m) break;
    if (r == t) { s = c; continue; }

    // instead of reducing above the block to compute the Z table,
    // we'll figure it out using an array of indices...

    // first, clear Z[0]
    z[0] = 0;
    for (int w = s/64; w < wds; w++)
      Z[0][w] = 0;

    // now, for each pivot 0,...,r-t-1
    assert(r - t <= c-s && c-s <= S);
    for (int i = 0; i < r - t; i++) {
      int ii = 1<<i;
      int vv = bits(A[t+i], s, c-s);
      // copy block of size 2^i and xor i-th row onto it
      for (int j = 0; j < ii; j++) {
        int a = z[j], b = a ^ vv;
        z[j+ii] = b;
        addrows (Z[b], Z[a], A[t+i], s/64, wds);
      }
    }

    // now reduce below this block
    for (int i = r; i < m; i++) {
      int v = bits(A[i], s, c-s);
      addrows (A[i], A[i], Z[v], s/64, wds);
    }
    s = c;

  }

  // at this point, we have rows down to r in upper-triangular
  // form and have cleared below them. There weren't enough
  // columns left for the 4 Russians method.

  int c = s; // column to probe for pivot
  for (; r < m && c < n;) {
    int j;
    for (j = r; j < m; j++)
      if (bit(A[j], c))
        break;
    if (j == m) { c++; continue; }
    if (j > r)
      for (int w = c/64; w < wds; w++)
        A[r][w] ^= A[j][w];
    assert(bit(A[r],c));
    if (piv) piv[r] = c;
    for (j = r+1; j < m; j++)
      if (bit(A[j], c))
        for (int w = c/64; w < wds; w++)
          A[j][w] ^= A[r][w];
    r++, c++;
  }

  free(Z);
  free(z);
  return r;
}

static int kernel(const int m,    // height (rows)
                  const int n,    // width in bits
                  const int wds,  // width in words
                  const uint64_t (*A)[wds],
                  const int *piv, // pivots computed during semi-ech
                  const int k,    // number of kernel rows requested
                  uint64_t (*K)[wds]) {

  // returns number of kernel vectors set
  // assumes already in semi-echelon form with pivots in piv[]

  int i = 0;
  for (int r = 0, j = 0; j < n && i < k; i++, j++) {

    // scan forward for next missing pivot:
    while (r < m && j == piv[r])
      j++, r++;
    if (j == n) break;

    for (int w=0; w<wds; w++) K[i][w] = 0;
    K[i][j/64] ^= 1UL<<(j%64);

    // backsolve
    for (int l=r-1; l>=0; l--) {
      int p = piv[l];
      if (dotprod(A[l], K[i], p/64, j/64+1))
        K[i][p/64] ^= 1UL<<(p%64);
    }
  }

  return i; // number of kernel vectors returned
}

int solution(const int m,    // height (rows)
             const int n,    // width in bits
             const int wds,  // width in words
             const uint64_t (*A)[wds],
             const int *piv, // pivots computed during semi-ech
             const int b,    // number of equation columns
             uint64_t (*X)[wds]) {

  // returns number of solutions; -1 for failure
  // assumes already in semi-echelon form with pivots in piv[]
  // rhs bits are assumed to be bits n,n+1,...,n+b-1

  assert(b > 0 && b <= 64);
  assert(n > 0 && wds > 0);
  assert(((size_t)n+b+63)/64 <= (size_t)wds);

  int r; // compute rank
  for (r = 0; r < m; r++)
    if (piv[r] < 0) break;

  // check for failure of system
  for (int i = r; i < m; i++)
    if (bits(A[i],n,b) != 0)
      return -1; // failure

  if (b >= 32 && r >= 16) {
    // Y holds the remaining RHS bits for each pivot row
    uint64_t *Y = malloc(((size_t)r + 1) * sizeof *Y);
    assert(Y);
    for (int i = 0; i < r; i++) {
      assert(piv[i] >= 0 && piv[i] < n);
      assert(i == 0 || piv[i-1] < piv[i]);
      Y[i] = bits(A[i], n, b);
    }
    for (int i = 0; i < b; i++)
      for (int w = 0; w < wds; w++) X[i][w] = 0;

    // solve a block, then clear it from the earlier RHS values
    const int S = 8;
    uint64_t Z[1<<S];
    for (int k = r; k > 0;) {
      const int c = piv[k-1]+1, s = c > S ? c-S : 0;
      uint64_t U[S];
      for (int i = 0; i < S; i++) U[i] = 0;
      int l = k;
      while (l > 0 && piv[l-1] >= s) l--;
      for (int i = k-1; i >= l; i--) {
        uint64_t v = Y[i];
        for (int j = i+1; j < k; j++)
          if (bit(A[i], piv[j])) v ^= U[piv[j]-s];
        const int p = piv[i];
        U[p-s] = v;
        for (; v; v &= v-1)
          X[stdc_trailing_zeros_ull(v)][p/64] |= 1UL<<(p%64);
      }
      Z[0] = 0;
      for (int i = 0; i < c-s; i++) {
        const int ii = 1<<i;
        for (int j = 0; j < ii; j++) Z[j+ii] = Z[j] ^ U[i];
      }
      for (int i = 0; i < l; i++)
        Y[i] ^= Z[bits(A[i], s, c-s)];
      k = l;
    }
    free(Y);
    return b;
  }

  for (int i = 0, j = n; i < b; i++, j++) {

    for (int w=0; w<wds; w++) X[i][w] = 0;

    X[i][j/64] ^= 1UL<<(j%64); // set rhs bit

    // backsolve
    for (int l=r-1; l>=0; l--) {
      int p = piv[l];
      assert(p >= 0);
      assert(p < n);
      if (dotprod(A[l], X[i], p/64, j/64+1))
        X[i][p/64] ^= 1UL<<(p%64);

    }

    X[i][j/64] ^= 1UL<<(j%64); // clear rhs bit
  }

  return b;
}

// ----------------------------------------------------------------------
// splitmix64 (Steele--Lea--Flood, via Vigna)

uint64_t state[2] = {3141592653589793238UL, 2718281828459045235UL};
static uint64_t rand64(void) {
  uint64_t z = (state[0] += 0x9E3779B97F4A7C15ULL) ^ state[1];
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}


#include "gf2.h"


void dump(const int m, const int n, const int b, const int wds,
          uint64_t A[m][wds]) {
  for (int r = 0; r < m; r++) {
    printf(" [%03d] ", r);
    for (int i = 0; i < n; i++)
      printf("%u", (unsigned)bit(A[r],i));
    if (b) {
      printf("|");
      for (int i = n; i < n+b; i++)
        printf("%u", (unsigned)bit(A[r],i)); }
    printf("\n"); } }


void test_interface () {

  {
    gf2_t *pMatrix = calloc(1, sizeof(gf2_t));
    pMatrix->m = 1;
    pMatrix->n = 64;
    pMatrix->b = 7;
    gf2_init(pMatrix);

    ((uint64_t *)pMatrix->matrix)[0] = 1;
    ((uint64_t *)pMatrix->matrix)[1] = 1;

    printf("doing Sean\n");
    gf2_semi_ech(pMatrix);
    gf2_info(pMatrix);

    printf("done with Sean\n");
    gf2_clear(pMatrix);
    free(pMatrix); }


  { // allocations handled by code
    gf2_t X = { .n = 100, .m = 90, .b = 4 };
    gf2_init(&X);

    const int wds = X.wds;          // this extracts
    uint64_t (*A)[wds] = X.matrix;  // matrix in form
    assert(A);                      // to easily use

    gf2_randomize(&X);
    gf2_semi_ech(&X);
    gf2_kernel(&X);
    gf2_solution(&X);
    gf2_info(&X);
    gf2_clear(&X); }


  { // user managed data
    const int n = 100, m = 90, wds = 3, k = 12, b = 4;
    uint64_t (*A)[wds] = malloc(m * sizeof *A); // matrix
    uint64_t (*B)[wds] = malloc(m * sizeof *B); // matrix copy
    uint64_t (*K)[wds] = malloc(k * sizeof *A); // kernel
    uint64_t (*W)[wds] = malloc(b * sizeof *W); // solution
    int *piv = malloc(m * sizeof *piv);         // pivots
    for (int r = 0; r < m; r++)
      for (int w = 0; w < wds; w++)
        B[r][w] = A[r][w] = rand64(); // keep a copy

    // define the gf2 linear algebra context
    gf2_t X = { .n = n, .m = m, .wds = wds, .kmax = k, .b = 4,
                .matrix = A, .kernel = K, .pivots = piv,
                .solution = W };

    gf2_init(&X); // set it up
    int r = gf2_kernel(&X);   // returns number of kernel vectors
    int s = gf2_solution(&X); // returns number of rhs columns on success
    assert(s == b);
    gf2_info(&X);  // dump info
    gf2_clear(&X); // user data not touched in this form

    printf("system:\n");   dump(m, n, b, wds, A);
    printf("kernel:\n");   dump(r, n, b, wds, K);
    printf("solution:\n"); dump(b, n, b, wds, W);

    // check kernel with original copy
    for (int i = 0; i < r; i++)
      for (int j = 0; j < m; j++)
        assert(dotprod(K[i], B[j], 0, wds) == 0);

    // check solution with original copy
    for (int i = 0; i < m; i++) {
      uint64_t lhs = 0, rhs = bits(B[i], n, b);
      for (int j = b-1; j >= 0; j--)
        lhs = (lhs<<1) + dotprod(B[i], W[j], 0, wds);
      assert(lhs == rhs); }

    free(A); free(B);
    free(K); free(W);
    free(piv); } }


// ----------------------------------------------------------------------

#include <time.h>
static double wall () {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec/1e9; }


int main (int argc, char *argv[]) {

  for (++argv; --argc; ++argv)
    if (index(*argv, '='))
      putenv(*argv);

  if (1) test_interface();

  int n      = atoi(getenv("n")      ?: "4096");
  int m      = atoi(getenv("m")      ?: "0");
  int trials = atoi(getenv("trials") ?: "1000");
  int seed   = atoi(getenv("seed")   ?: "0");
  int S      = atoi(getenv("S")      ?: "8");

  if (m == 0) m = n;
  if (seed == 0) seed = time(0);
  state[0] = state[1] = seed;

  assert(n > 0 && m > 0 && trials > 0);
  assert((unsigned)S < sizeof(int)*CHAR_BIT - 1);
  const int wds  = ((size_t)n + 63) / 64;
  uint64_t (*A)[wds] = malloc(m * sizeof *A); // A[m][wds];
  assert(A);

  printf("m=%d n=%d (S=%d trials=%d)\n", m, n, S, trials);

  for (int r = 0; r < m; r++)
    for (int w = 0; w < wds; w++)
      A[r][w] = rand64();

  volatile double tm = 0;
  double rr = 0;

  uint64_t min_tm1 = ~0UL;
  int count = 0;
  for (; count < trials; count++) {

    for (int r = 0; r < m; r++)
      for (int w = 0; w < wds; w++)
        A[r][w] = rand64();

    tm -= wall();
    volatile uint64_t tm1 = rdtsc();
    int r = semi_ech(m, n, wds, A, S, NULL);
    tm1 = rdtsc() - tm1;
    if (tm1 < min_tm1) min_tm1 = tm1;
    tm += wall();

    rr += r; }

  tm /= count;
  rr /= count;

  printf("average rank = %.2f\n", rr);

  if (tm < 1e-6)       printf("avg %.8f microseconds\n", tm*1e6);
  else if (tm < 1e-3)  printf("avg %.8f milliseconds\n", tm*1e3);
  else                 printf("avg %.8f seconds\n", tm);
  printf("min tm1 = %llu\n", (unsigned long long)min_tm1);

  free(A);

  return 0; }

