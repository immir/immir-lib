#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <math.h>
#include <assert.h>

#include <immintrin.h>

// ----------------------------------------------------------------------
// --- static helper functions should be inlined

static unsigned long long rdtsc (void) {
  unsigned long long tick;
  __asm__ __volatile__("rdtsc":"=A"(tick));
  return tick;
}

static uint64_t bits (uint64_t *V, int i, int b) {
  int w0 = i/64, w1 = (i+b-1)/64, ii = i%64;
  uint64_t x = V[w0] >> ii;
  if (w0 < w1)
    x ^= V[w1] << (64 - ii);
  x &= (1UL<<b)-1;
  return x;
}

static uint64_t bit (uint64_t *V, int i) {
  // guessing that inlined code using this with the same
  // i multiple times will reuse the computed probe...
  uint64_t probe = 1UL << (i%64);
  return (V[i/64] & probe) != 0;
}

static void addrows (uint64_t * restrict A, uint64_t * restrict B, uint64_t * restrict C, int s, int wds) {
  for (int w = s; w < wds; w++)
    A[w] = B[w] ^ C[w];
}

static uint64_t dotprod (uint64_t *A, uint64_t *B, int w0, int w1) {
  uint64_t x = 0;
  for (int w = w0; w < w1; w++)
    x ^= A[w] & B[w];
  return __builtin_parityl(x);
}

// ----------------------------------------------------------------------
// main worker functions

static int semi_ech(const int m, const int n, const int wds, uint64_t (*A)[wds],
                    int S, int *piv) {

  // 4 Russians with S bit tables; Z[] holds the precomputed row sums,
  // z[] the indexing into Z[] required since we don't reduce above the
  // block diagonal...

  if (S == 0) S = log(m); // reasonable default

  const int SS = (1<<S);
  uint64_t (*Z)[wds] = malloc(SS * sizeof *Z); // Z[SS][wds]
  int *z = malloc(SS * sizeof *z);
  assert(Z);
  assert(z);

  int r = 0; // row in reduction
  int s = 0; // block start for 4 Russians table

  if (piv) for (int r=0; r<m; r++) piv[r] = -1;

  /* 
     This diagram summarises the method. We use "4 Russians" tables
     of width S bits (S=3 below); current block starts at column s;
     to find pivot for row r, we start at j=r and reduce to the
     left using pivots s .. r-1; then check whether row j has a
     pivot for row r -- if it does, (possibly) xor it onto row r.
     Once the block is complete, form the table of size 2^S and
     reduce below...

         +-----+--------------------------------+
         |1 * *|* * * * * * * * * * * * * * * * |
         |  1 *|* * * * * * * * * * * * * * * * |
         |    1|* * * * * * * * * * * * * * * * |
         +-----+-----+--------------------------+
     s-> |     |1 * *|* * * * * * * * * * * * * |
         |     |0 0 *|* * * * * * * * * * * * * | <- r
         |     |0 0 *|* * * * * * * * * * * * * |
         |     |* * *|* * * * * * * * * * * * * | <- j
         |     |* * *|* * * * * * * * * * * * * |
         |     |* * *|* * * * * * * * * * * * * |
         +--------------------------------------+

      On failure to find a pivot, break out for a simpler loop
      that finishes it off.

   */

  for (; S > 1 && s + S <= m && r == s; s += S) {

    // S columns at a time for 4 Russians method

    // NB: when we break out of following loop because there
    // is no pivot for column r, then the condition r==s above
    // will be violated.

    for (r = s; r < s + S; r++) {

      // find a row with pivot in column r
      int j;
      for (j = r; j < m; j++) {
        for (int k = s; k < r; k++)
          // reduce relative to this block using pivots found so far
          if (bit(A[j], k))
            addrows (A[j], A[j], A[k], s/64, wds);
        // now check for new pivot
        if (bit(A[j], r)) break;
      }

      if  (j == m)
        // no pivot in this column, skip to next section
        break;

      if (j != r) 
        // xor onto row r to get pivot there (if it's not already)
        addrows (A[r], A[r], A[j], s/64, wds);

      if (piv) piv[r] = r;
    }
  
    // missing pivot; break out to clean up loop
    if (r != s + S) break;

    // we have a full block of S new pivots, let's reduce below
    // this block -- unless there are no rows below us...
    if (s + S == m) break;

    // instead of reducing above the block to compute the Z table,
    // we'll figure it out using an array of indices...

    // first, clear Z[0]
    z[0] = 0;
    for (int w = s/64; w < wds; w++)
      Z[0][w] = 0;
    
    // now, for each pivot 0,...,S-1
    for (int i = 0; i < S; i++) {
      int ii = 1<<i;
      int vv = bits(A[s+i], s, S);
      // copy block of size 2^i and xor i-th row onto it
      for (int j = 0; j < ii; j++) {
        int a = z[j], b = a ^ vv;
        z[j+ii] = b;
        addrows (Z[b], Z[a], A[s+i], s/64, wds);
      }
    }

    // now reduce below this full-rank block
    for (int i = s + S; i < m; i++) {
      int c = bits(A[i], s, S);
      addrows (A[i], A[i], Z[c], s/64, wds);
    }

  }

  // at this point, we have rows down to r in upper-triangular
  // form and have cleared below them. We are either missing a
  // pivot, or didn't have enough columns for the 4 Russians method.

  int c = r; // column to probe for pivot
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
                  uint64_t (*A)[wds],
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
             uint64_t (*A)[wds],
             const int *piv, // pivots computed during semi-ech
             const int b,    // number of rhs columns
             uint64_t (*X)[wds]) {

  // returns number of solutions; -1 for failure
  // assumes already in semi-echelon form with pivots in piv[]
  // rhs bits are assumed to be bits n,n+1,...,n+b-1

  assert((n+b+63)/64 <= wds);

  int r; // compute rank
  for (r = 0; r < m; r++)
    if (piv[r] < 0) break;

  // check for failure of system
  for (int i = r; i < m; i++)
    if (bits(A[i],n,b) != 0)
      return -1; // failure

  for (int i = 0, j = n; i < b; i++, j++) {

    for (int w=0; w<wds; w++) X[i][w] = 0;

    X[i][j/64] ^= 1UL<<(j%64); // set rhs bit
    
    // backsolve
    for (int l=r-1; l>=0; l--) {
      int p = piv[l];
      assert(p >= 0);
      assert(p < n);
      if (dotprod(A[l], X[i], 0, wds))
        X[i][p/64] ^= 1UL<<(p%64);

    }

    X[i][j/64] ^= 1UL<<(j%64); // clear rhs bit
  }

  return b;
}


// ----------------------------------------------------------------------
// xorshiro128+ code, authored by David Blackman and Sebastiano Vigna

uint64_t state[2] = {3141592653589793238UL, 2718281828459045235UL};
static inline uint64_t rotl(const uint64_t x, int k) {
  return (x << k) | (x >> (64 - k)); }
static uint64_t rand64(void) {
  uint64_t s0 = state[0], s1 = state[1];
  const uint64_t result = s0 + s1; s1 ^= s0;
  state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14);
  state[1] = rotl(s1, 36); return result; }


#include "gf2.h"


void dump(const int m, const int n, const int b, const int wds,
          uint64_t A[m][wds]) {
  for (int r = 0; r < m; r++) {
    printf(" [%03d] ", r);
    for (int i = 0; i < n; i++)
      printf("%ld", bit(A[r],i));
    if (b) { 
      printf("|");
      for (int i = n; i < n+b; i++)
        printf("%ld", bit(A[r],i));
    }
    printf("\n");
  }
}


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
  }


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
    gf2_clear(&X);

  }


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
    int r = gf2_kernel(&X);   // returns rank
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
    for (int i = 0; i < r; i++) {
      uint64_t lhs = 0, rhs = bits(B[r], n, b);
      for (int j = b-1; j >= 0; j--)
        lhs = (lhs<<1) + dotprod(B[r], W[j], 0, wds);
      if (lhs != rhs)
        printf("i=%d lhs=%lx rhs=%lx\n", i, lhs, rhs);
    }

    free(A); free(B);
    free(K); free(W);
    free(piv);
  }

}

// ----------------------------------------------------------------------

#include <time.h>
static double wall () {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return t.tv_sec + t.tv_nsec/1e9;
}

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

  const int wds  = (n + 63) / 64;
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

    rr += r;
  }

  tm /= count;
  rr /= count;

  printf("average rank = %.2f\n", rr);

  if (tm < 1e-6)       printf("avg %.8f microseconds\n", tm*1e6);
  else if (tm < 1e-3)  printf("avg %.8f milliseconds\n", tm*1e3);
  else                 printf("avg %.8f seconds\n", tm);
  printf("min tm1 = %ld\n", min_tm1);

  free(A);

  return 0;
}
