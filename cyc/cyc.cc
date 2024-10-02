#if 0
set -x
g++ -std=c++17 -march=native -g -Wall -Wextra -Werror -O2 -fopenmp -o ${0%.*} $0 -lm && ./${0%.*}
exit
#endif

/*
  Recursive, multi-threaded permutation cycle structure code demo
  ---------------------------------------------------------------
  July 2021, Michael "Immir" Smith <michael@immir.com>

  This program demonstrates a very simple multi-threaded algorithm for
  computing the cycle structure of a permutation. The algorithm works
  by recursively reducing the problem to "permutations" of half the
  original degree; we are really working with the obvious weighted
  directed graph corresponding to the permutation and collapsing or
  aggregating paths to remove vertices, recursively...

  In addition to clearly providing a multi-threaded speedup, this
  method turns out to be faster even on a single thread; this speedup
  appears to be related to branch-prediction and speculation allowing
  future remote loads to be speculatively executed ahead of time so
  values end up in cache ahead of time (at least that's my speculation).

  The algorithm works as indicated by the following toy example:

  +-0-+-1-+-2-+-3-+-4-+-5-+-6-+-7-+-8-+-9-+-A-+-B-+-C-+-D-+-E-+-F-+
  | 6 | C | 9 | A | 3 | 8 | 2 | B | 4 | E | 1 | 7 | 5 | F | 0 | D |
  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

  First, augment the permutation with edge lengths, all equal to 1 to
  start with.

  +-0-+-1-+-2-+-3-+-4-+-5-+-6-+-7-+-8-+-9-+-A-+-B-+-C-+-D-+-E-+-F-+
  | 6 | C | 9 | A | 3 | 8 | 2 | B | 4 | E | 1 | 7 | 5 | F | 0 | D |
  +-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+-1-+

  Now we recursively iterate a pair of phases:

  Phase 1:

  For each entry in the first half, if it goes to the first half,
  leave it untouched. If it goes to the second half, trace out its
  images until it comes back to the first half again, and update the
  image and length as appropriate. While visiting the second half,
  strike out any entries traversed.  For example, 0 -> 6 we leave
  alone; 2 -> 9 -> E -> 0, so update 2 to point to 0 with length 1
  + 1 + 1 = 3. Note that half the time we do nothing, a quarter of
  the time we visit just one high entry, etc...

  The result is:

  +-0-+-1-+-2-+-3-+-4-+-5-+-6-+-7-+-8-+-9-+-A-+-B-+-C-+-D-+-E-+-F-+
  | 6 | 5 | 0 | 1 | 3 | 4 | 2 | 7 | . | . | . | . | . | F | . | D |
  +-1-+-2-+-3-+-2-+-1-+-2-+-1-+-2-+---+---+---+---+---+-1-+---+-1-+

  Phase 2:

  Now scan through the second half for entries not struck out; we find
  D -> F -> D, a 2-cycle (adding the edge lengths 1 + 1 = 2). Note by
  keeping track of how many entries were struck out, we know whether
  or not we need to do this phase at all.

  We now have a permutation on only 8 points, with lengths associated
  to each image - so it's really a weighted directed graph:

  +-0-+-1-+-2-+-3-+-4-+-5-+-6-+-7-+
  | 6 | 5 | 0 | 1 | 3 | 4 | 2 | 7 |
  +-1-+-2-+-3-+-2-+-1-+-2-+-1-+-2-+

  Now just recurse:

  +-0-+-1-+-2-+-3-+-4-+-5-+-6-+-7-+
  | 2 | 3 | 0 | 1 | . | . | . | 7 |
  +-2-+-5-+-3-+-2-+---+---+---+-2-+

  2 cycle with 7 as rep.

  +-0-+-1-+-2-+-3-+
  | 0 | 1 | . | . |
  +-5-+-7-+---+---+

  +-0-+-1-+
  | 0 | 1 |
  +-5-+-7-+

  7 cycle with rep 1.

  +-0-+
  | 0 |
  +-5-+

  5 cycle with rep 0.

  That's two 2-cycles, a 5-cycle and a 7-cycle.

  Every time we resolve a cycle we recover both its length and the
  minimal representative on it.

  Checking in Magma, we can see the result is correct:

  > S := Sym({0..15});
  > pi := S![6,12,9,10,3,8,2,11,4,14,1,7,5,15,0,13];
  > CycleStructure(pi);
  [ <7, 1>, <5, 1>, <2, 2> ]
  > pi;
  (0, 6, 2, 9, 14)(1, 12, 5, 8, 4, 3, 10)(7, 11)(13, 15)

  This demo file is a crazy(?) mixture of C and C++, but the core
  algorithms are implemented essentially in plain C for simplicity
  using OpenMP for trivial but effective parallelisation.

  It also contains a internal "makefile" rule; just run this source file
  as a shell script:

  $ sh cyc.cc

  This will compile and (if successful) run a test.

*/

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <omp.h>

#include <map>
#include <vector>

#define BITS 64
#if BITS==64
typedef int64_t val_t;
#define leadz __builtin_clzl
#elif BITS==32
typedef int32_t val_t;
#define leadz __builtin_clz
#endif

#include <time.h>
static long rtc () {
  struct timespec ts;
  assert(-1 != clock_gettime(CLOCK_MONOTONIC, &ts));
  return ts.tv_sec * 1e9 + ts.tv_nsec; }

#define streq(a,b) (strcmp(a,b)==0)
#define maskr(n) ((1UL<<n)-1)
#define likely(x) __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)
#define global
global double g_z; // HACK

#include <time.h>
static double wall () {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return t.tv_sec + t.tv_nsec/1e9; }

typedef std::map<val_t,val_t> cyc_t;

// ----------------------------------------------------------------
// create a random permutation using forward exchange

void make_perm (val_t *perm, val_t n, int seed) {
  for (val_t i = 0; i < n; i++)
    perm[i] = i;
  srand48(seed);
  for (val_t i = 0; i < n-1; i++) {
    val_t j = i + lrand48() % (n-i);
    val_t t = perm[i];
    perm[i] = perm[j];
    perm[j] = t; } }

// ----------------------------------------------------------------
// check that an array is a permutation

static void check_perm(val_t *p, val_t n) {
  std::vector<bool> q(n);
  for (val_t x = 0; x < n; x++) {
    val_t y = p[x];
    assert(y < n);
    if (q[y])
      assert(!"not a permutation!");
    q[y] = true; } }

// ----------------------------------------------------------------
// naive version, equivalent to the algorithm implemented in Magma,
// with more-or-less identical performance to Magma's

static cyc_t cyc_naive(val_t *perm, val_t n) {

  cyc_t c;

  // allocate array of bitmasks for points seen so far

  val_t M = (n + 63)/64;
  ulong *m = (ulong *) calloc(M, sizeof *m);
  assert(m);

  for (val_t w = 0; w < M; ) {
    // skip over bitmask words which are entirely set
    if (~m[w] == 0UL) { w++; continue; }
    // find the first unused value
    val_t x = w*64 + __builtin_ctzl(~m[w]);
    if (x >= n) break; // we are done
    // now chase out this new cycle
    val_t y = x, l = 0;
    do {
      m[y/64] |= 1UL<<(y&63);
      y = perm[y]; l++;
    } while (y != x);
    // new cycle of length l:
    c[l]++; }

  free(m);
  return c; }

// ----------------------------------------------------------------
// simple threaded

static cyc_t cyc_immir(val_t *perm, val_t n) {

  cyc_t c;

  // allocate and copy array as this algorithm is destructive!
  // (there are fallback options if insufficient memory)
  // we need a copy of the permuation augmented with a length
  // field for each entry; strictly speaking we only need it
  // for half the permutation (the first half), but for simplicity
  // we'll ignore that optimisation here...

  struct ptlen { val_t pt, len; };
  struct ptlen *p = (struct ptlen *) malloc(n * sizeof *p);
  assert(p);

  #pragma omp parallel for schedule(static)
  for (val_t i = 0; i < n; i++)
    p[i] = (struct ptlen) { perm[i], 1 };

  // we will "recursively" reduce the permutation to permutations
  // on the first half of the domain, and check the second half
  // for cycles supported on it (only if we detect that they exist)

  for (val_t m = g_z*n; n > 1; n = m, m = g_z*n) { // reduce from degree n to degree m
    if (m == 0) m = 1;

    val_t top = 0; // track total of second half entries visited

    // for each entry in the first half, if it goes into the
    // second half then trace it out until it returns to the
    // first half and update the image (and length), marking
    // those entries skipped over by clearing their length field.

    #pragma omp parallel for schedule(static) reduction(+:top)
    for (val_t x = 0; x < m; x++) {
      val_t y = p[x].pt, l = p[x].len;
      if (likely(y < m)) continue;
      while (y >= m)
        l += p[y].len, p[y].len = 0, y = p[y].pt, ++top;
      p[x] = (struct ptlen) { y, l }; }

    if (top == n - m) continue; // no small cycles to find; skip phase 2

    // Now scan through p[m..n) to find entries that are on cycles
    // supported in this half -- i.e. entries whose length field is
    // still non-zero after the previous phase...

    // NB: No benefit from multi-threading this loop in the random
    // permutation case at least
    for (val_t x = m; x < n && top < n-m; x++) {

      if (p[x].len == 0) continue; // part of an arc from first phase

      // trace out a cycle from x that we now know is supported
      // on the second part of the domain...

      val_t y = x, l = 0;
      do {
        l += p[y].len, p[y].len = 0;
        y = p[y].pt;
        top ++;
      } while (y > x);

      assert (y == x); // without threading, x will be minimal

      c[l]++; } }

  // we are down to a singleton now:
  assert(p[0].pt == 0);
  { val_t l = p[0].len;
    c[l]++; }

  free(p);
  return c; }


// ----------------------------------------------------------------

int64_t parse (const char *str) {
  long b, e=1;
  if (sscanf(str, "%ld^%ld\n", &b, &e)>0)
    return pow(b,e);
  assert(!"bad parse"); }

// ----------------------------------------------------------------

int main (int argc, char *argv[]) {

  --argc, ++argv;

  while (argc && strchr(*argv,'='))
    putenv(*argv), --argc, ++argv;

  val_t n = parse(getenv("n") ?: "2^24");
  val_t T = parse(getenv("T") ?: "1");

  printf("n = %ld\n", (long)n);
  printf("T = %ld (trials)\n", (long)T);
  printf("BITS = %d\n", BITS);

  const char *teststr = argc > 0 ? *argv : "all";

  int nthreads = 1;
  #pragma omp parallel
  { nthreads = omp_get_num_threads(); }
  printf("omp_get_num_threads() = %d\n", nthreads);

  global g_z = atof(getenv("z") ?: "0.75");

  { struct timespec ts;
    assert(-1 != clock_getres(CLOCK_MONOTONIC, &ts));
    printf("clock resolution: %ld ns\n",
           (long)(ts.tv_sec * 1e9 + ts.tv_nsec)); }

  val_t *perm = (val_t *) malloc(n * sizeof *perm);
  assert(perm);

  // ------------------------------------------------------------
  // make permutation

  if (getenv("read")) {
    double tm = wall();
    int fd = open(getenv("read"), O_RDONLY);
    assert(fd != -1);
    uint64_t chunk = n/64;
    uint64_t bytes = 0;
    #pragma omp parallel for reduction(+:bytes)
    for (int i = 0; i < 64; i++)
      bytes += pread(fd, (void*)&perm[i*chunk], chunk * sizeof *perm,
		     i*chunk*sizeof *perm);
    assert(bytes == n * sizeof *perm);
    close(fd);
    tm = wall() - tm;
    printf("read perm: %.2g seconds\n", tm); }
  else {
    uint seed = (rtc() & 0xfff) | 1;
    if (getenv("seed")) seed = atoll(getenv("seed"));
    printf("seed = %d\n", seed);
    double tm = wall();
    make_perm(perm, n, seed);
    tm = wall() - tm;
    printf("make perm: %.2g seconds\n", tm);
    tm = wall();
    check_perm(perm, n);
    tm = wall() - tm;
    printf("check perm: %.2g seconds\n", tm); }

  if (getenv("write")) {
    double tm = wall();
    int fd = open(getenv("write"), O_WRONLY|O_CREAT, 0777);
    assert(fd != -1);
    uint64_t chunk = n/64;
    uint64_t bytes = 0;
    #pragma omp parallel for reduction(+:bytes)
    for (int64_t i = 0; i < 64; i++)
      bytes += pwrite(fd, (void*)&perm[i*chunk], chunk * sizeof *perm,
		      i*chunk*sizeof *perm);
    assert(bytes == n * sizeof *perm);
    close(fd);
    tm = wall() - tm;
    printf("wrote perm: %.2g seconds\n", tm); }

  auto test = [perm,n,T] (auto name, auto fn) {
    double tm = wall();
    cyc_t c;
    for (int t = 0; t < T; ++t)
      c = fn(perm, n);
    tm = wall() - tm;
    tm /= T;
    val_t tot = 0;
    for (auto const & [k, v] : c) {
      tot += k*v;
      printf(v==1 ? "%ld " : "%ld^%ld ", (long)k, (long)v); }
    printf("\n%s: %.2g\n", name, tm);
    assert(tot == n);
    return tm; };

  if (streq(teststr, "all")) {
    double tm_naive = getenv("tm_naive") ? atof(getenv("tm_naive")) :
      test("naive", cyc_naive);

    double tm_immir = test("immir", cyc_immir);
    printf("speedup(immir): %.2f x (%d threads, z=%.2f, T=%ld trials)\n",
           tm_naive / tm_immir, nthreads, g_z, (long)T); }

  else if (streq(teststr, "naive"))
    test("naive", cyc_naive);

  else if (streq(teststr, "immir"))
    test("immir", cyc_immir);

  free(perm);
  return 0; }
