// plethy.cpp — fast Schur-function plethysm  s_a[s_b]  expanded in the Schur basis.
//
// Algorithm (re-expression of Symmetrica's plet.c, see README.md):
//   * Terms are partitions stored as beta-number (Maya) bitsets:
//       beta_i = lambda_i + (N - i),  i = 1..N slots  =>  set of distinct bits.
//   * Core kernel:  E * p_k[s_G]  via Muir's rule: for every monomial weight
//     mu of s_G (with Kostka multiplicity), move particles up by k*mu_v;
//     collision => 0; sign = parity of particles crossed (Muir, eq. 3.2.3).
//   * Outer single row/column: Newton recurrence
//       j * h_j[f] = sum_{i=1..j}            p_i[f] * h_{j-i}[f]
//       j * e_j[f] = sum_{i=1..j} (-1)^(i-1) p_i[f] * e_{j-i}[f]
//   * General outer: power-sum route
//       s_A[f] = sum_{rho |- |A|} (chi^A_rho / z_rho) prod_j p_{rho_j}[f]
//     (integer arithmetic over common denominator |A|!).
//   * omega bookkeeping: choose the cheaper inner orientation (square-free
//     e-type inner is cheapest);  omega(f[g]) = f[w g] (|g| even),
//     (w f)[w g] (|g| odd)  =>  conjugate outputs at the end.
//
// Support bounds (proved via the max-monomial argument + omega duality):
//   lambda_1 <= |A| * G_1   and   l(lambda) <= |A| * l(G)
// hold for the answer and all intermediate stages, so the bitset width is
//   N = min(n, |A|*l(G)) slots,  max bit = min(n,|A|*G_1) + N - 1.

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <map>
#include <new>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_OPENMP)
#error "plethy requires OpenMP; build with -fopenmp"
#endif
#include <omp.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "beta_core.hpp"

using std::size_t;
using std::uint64_t;
using std::int64_t;
using i128 = __int128;

// Bead-count / bit-position cap.  The beta-number key is at most 4 words, so
// maxbit <= 255 (main rejects words > 4) and, since maxbit >= N - 1, the bead
// count N <= 256.  Every per-bead fast-path array is sized to this; inputs
// that would exceed it are rejected loudly rather than overrunning a buffer.
static constexpr int MAX_SLOTS = 4 * 64;   // 256

// ----------------------------------------------------------------- utilities

static std::atomic<bool> g_overflow{false};
static bool g_verbose = false;
static bool g_dump = false;                     // PLETHY_DUMP: trace signed configs
static std::atomic<uint64_t> g_emissions{0};   // verbose-mode statistics only
static int g_threads_small = 64;     // many small/mid passes: sync-bound
static int g_threads_big = 192;      // huge passes: DRAM-latency-bound, SMT helps

static inline int64_t addc(int64_t a, int64_t b) {
  if (int64_t r; !__builtin_add_overflow(a, b, &r)) return r;
  g_overflow.store(true, std::memory_order_relaxed);
  return 0; }

static inline int64_t mulc(int64_t a, int64_t b) {
  if (int64_t r; !__builtin_mul_overflow(a, b, &r)) return r;
  g_overflow.store(true, std::memory_order_relaxed);
  return 0; }

static inline i128 addc(i128 a, i128 b) { return a + b; }   // 2^126 headroom
static inline i128 mulc(i128 a, i128 b) { return a * b; }


// ----------------------------------------------------------------- partitions


static int psum(const Part& p) { int s = 0; for (int v : p) s += v; return s; }

static Part conj_part(const Part& p) {
  Part c;
  if (p.empty()) return c;
  c.resize(p[0], 0);
  for (int v : p)
    for (int j = 0; j < v; ++j) c[j]++;
  return c; }                              // weakly decreasing by construction

// all partitions of m, parts <= cap, descending-lex enumeration
static void partitions_of(int m, int cap, Part& cur, std::vector<Part>& out) {
  if (m == 0) { out.push_back(cur); return; }
  for (int q = std::min(m, cap); q >= 1; --q) {
    cur.push_back(q);
    partitions_of(m - q, q, cur, out);
    cur.pop_back(); } }

static std::vector<Part> partitions_of(int m) {
  std::vector<Part> out; Part cur;
  partitions_of(m, m, cur, out);
  return out; }

// ------------------------------------------------- Kostka numbers K_{lam,mu}
// number of SSYT of shape lam, content mu == number of chains of horizontal
// strips  0 = nu^0 < nu^1 < ... < nu^t = lam  with |nu^i/nu^{i-1}| = mu_i.

static long kostka_rec(const Part& lam, const Part& mu, size_t idx, Part& nu) {
  if (idx == mu.size()) {
    if (nu.size() > lam.size()) return 0;
    for (size_t r = 0; r < lam.size(); ++r)
      if ((r < nu.size() ? nu[r] : 0) != lam[r]) return 0;
    return 1; }
  const int want = mu[idx];
  const int rows = (int)lam.size();
  long total = 0;
  // enumerate horizontal strips nu -> nu2 of size `want` inside lam
  Part nu2(rows, 0);
  for (size_t r = 0; r < nu.size(); ++r) nu2[r] = nu[r];
  // DFS over rows, choosing additions a_r with nu2_r + a_r <= min(lam_r, prev row's old value)
  struct S {
    const Part& lam; const Part& mu; size_t idx;
    Part& nu2; const Part& nuOld; long total = 0;
    S(const Part& l, const Part& m, size_t i, Part& n2, const Part& no)
      : lam(l), mu(m), idx(i), nu2(n2), nuOld(no) {}
    void go(int r, int rem) {
      if (rem == 0) {
        // recurse to next content value with trimmed copy
        Part trimmed;
        for (int x : nu2) { if (x == 0) break; trimmed.push_back(x); }
        total += kostka_rec(lam, mu, idx + 1, trimmed);
        return;
      }
      if (r >= (int)lam.size()) return;
      const int oldv = (r < (int)nuOld.size()) ? nuOld[r] : 0;
      const int above = (r == 0) ? INT32_MAX : ((r - 1 < (int)nuOld.size()) ? nuOld[r - 1] : 0);
      const int hi = std::min(lam[r], above);     // strip: new_r <= old_{r-1}
      const int maxAdd = std::min(rem, hi - oldv);
      for (int a = maxAdd; a >= 0; --a) {
        nu2[r] = oldv + a;
        go(r + 1, rem - a); }
      nu2[r] = oldv; } };
  Part nuOld = nu;
  S s(lam, mu, idx, nu2, nuOld);
  s.go(0, want);
  total += s.total;
  return total; }

static long kostka(const Part& lam, const Part& mu) {
  Part nu;
  return kostka_rec(lam, mu, 0, nu); }

// --------------------------------------------- characters chi^lam_rho (MN rule)

static long chi_rec(uint64_t betas, int nbeta, const Part& rho, size_t idx) {
  if (idx == rho.size()) return 1;       // all boxes removed -> staircase left
  const int r = rho[idx];
  long total = 0;
  for (int b = 63; b >= r; --b) {
    if (!(betas >> b & 1)) continue;
    const int t = b - r;
    if (betas >> t & 1) continue;        // occupied -> no ribbon here
    uint64_t nb = (betas & ~(1ULL << b)) | (1ULL << t);
    // sign = (-1)^{# betas strictly between t and b}
    const uint64_t between = betas & ((1ULL << b) - 1) & ~((1ULL << (t + 1)) - 1);
    const int cross = __builtin_popcountll(between);
    const long sub = chi_rec(nb, nbeta, rho, idx + 1);
    total += (cross & 1) ? -sub : sub; }
  return total; }

static long chi_char(const Part& lam, const Part& rho) {
  assert(psum(lam) == psum(rho));
  const int L = (int)lam.size();
  uint64_t betas = 0;
  for (int i = 0; i < L; ++i) {
    const int b = lam[i] + (L - 1 - i);
    assert(b < 64);
    betas |= 1ULL << b; }
  if (L == 0) return 1;
  return chi_rec(betas, L, rho, 0); }

static int64_t z_of(const Part& rho) {
  int64_t z = 1;
  for (size_t i = 0; i < rho.size();) {
    size_t j = i;
    while (j < rho.size() && rho[j] == rho[i]) ++j;
    const int m = (int)(j - i);
    for (int t = 1; t <= m; ++t) z *= rho[i] * t;   // v^m * m!
    i = j; }
  return z; }

static int64_t factorial(int m) {
  int64_t f = 1;
  for (int i = 2; i <= m; ++i) f *= i;
  return f; }

// ------------------------------------------------------------ monomial table
// monomial expansion of s_G:  list of (mu, K_{G,mu}) for mu |- |G|, K > 0.
// Each entry stores distinct part values (descending) with multiplicities.

struct Mono {
  long K;
  int nv;                 // number of distinct values
  int val[16];            // distinct values, descending
  int cnt[16];            // multiplicities
  int total;              // total number of parts = sum cnt
};

static std::vector<Mono> monomials_of(const Part& G) {
  std::vector<Mono> out;
  for (const Part& mu : partitions_of(psum(G))) {
    if (!mu.empty() && mu[0] > G[0]) continue;       // K=0 unless mu <= G (dominance)
    const long K = kostka(G, mu);
    if (K == 0) continue;
    Mono m{};
    m.K = K; m.nv = 0; m.total = (int)mu.size();
    for (size_t i = 0; i < mu.size();) {
      size_t j = i;
      while (j < mu.size() && mu[j] == mu[i]) ++j;
      m.val[m.nv] = mu[i];
      m.cnt[m.nv] = (int)(j - i);
      ++m.nv;
      assert(m.nv <= 16);
      i = j; }
    out.push_back(m); }
  return out;
}

// ------------------------------------------------------------------ Key<W>


// --------------------------------------------------------------- hash map


// --------------------------------------------------------- sharded accumulator

template <int W, class C>
struct Accum {
  static constexpr int SH = 256;
  struct alignas(64) PaddedFlag { std::atomic_flag f; };
  std::array<Map<W, C>, SH> maps;
  std::array<PaddedFlag, SH> locks;

  Accum() { for (auto& l : locks) l.f.clear(); }

  static inline int shard_of(uint64_t h) { return (int)(h >> 56); }  // top 8 bits

  inline void add_serial(const Key<W>& k, C v) { maps[shard_of(key_hash(k))].add(k, v); }

  inline void lock(int s) {
    while (locks[s].f.test_and_set(std::memory_order_acquire))
      do { __builtin_ia32_pause(); } while (locks[s].f.test(std::memory_order_relaxed)); }

  inline void unlock(int s) { locks[s].f.clear(std::memory_order_release); }

  // total entries (upper bound; includes zero-coefficient entries)
  size_t total_cnt() const {
    size_t t = 0;
    for (const auto& m : maps) t += m.cnt;
    return t; }

  // pre-size each shard for ~totalEntries spread across shards
  void reserve_total(size_t totalEntries) {
    size_t per = totalEntries / SH * 2;          // target load <= 0.5
    if (per < 64) return;
    for (auto& m : maps)
      if (m.e.size() < per) {
        size_t cap = m.e.size();
        while (cap < per) cap <<= 1;
        std::vector<typename Map<W, C>::Entry> old = std::move(m.e);
        m.e.assign(cap, {Map<W, C>::empty_key(), C(0)});
        m.mask = cap - 1;
        const Key<W> EMP = Map<W, C>::empty_key();
        for (const auto& en : old) {
          if (en.k == EMP) continue;
          size_t i = key_hash(en.k) & m.mask;
          while (!(m.e[i].k == EMP)) i = (i + 1) & m.mask;
          m.e[i] = en; } } }

  // divide by div (exact), drop zeros, emit flat list
  List<W, C> finalize(int64_t div) {
    List<W, C> out;
    const Key<W> EMP = Map<W, C>::empty_key();
    size_t tot = total_cnt();
    out.keys.reserve(tot);
    out.coefs.reserve(tot);
    for (auto& m : maps) {
      for (const auto& en : m.e) {
        if (en.k == EMP || en.v == C(0)) continue;
        C v = en.v;
        if (div != 1) {
          if (v % C(div) != C(0)) {
            std::fprintf(stderr, "plethy: FATAL inexact division by %lld\n", (long long)div);
            std::exit(3); }
          v /= C(div); }
        out.keys.push_back(en.k);
        out.coefs.push_back(v); } }
    return out; }
};

// ----------------------------------------------------------------- the kernel
// acc += sigma * (in * p_k[s_G]),  monos = monomial expansion of s_G.

template <int W, class C>
struct PassCtx {
  int k;
  int maxbit;
  int lenZeroFloor = 0;           // prune len cap: drop key if lowest-zero < this
  Key<W> cur;
  int sign;                       // 0 / 1
  int srcPos[MAX_SLOTS];
  int nsrc;
  int nv;
  int val[16];
  int remc[16];
  C base;                         // c_in * sigma * K
  // emission sink
  Accum<W, C>* acc = nullptr;                     // serial mode
  std::vector<typename Map<W, C>::Entry>* bufs = nullptr;  // parallel: per-shard buffers
  Accum<W, C>* gacc = nullptr;
  static constexpr size_t FLUSH_N = 4096;

  inline void emit() {
    if (lenZeroFloor && key_lowest_zero(cur) < lenZeroFloor) return;   // length prune
    const C v = sign ? C(0) - base : base;
    if (g_dump) {                       // PLETHY_DUMP: one line per signed bead config
      int pos[MAX_SLOTS], n = 0;
      for (int wi = W - 1; wi >= 0; --wi) {
        uint64_t x = cur.w[wi];
        while (x) { const int b = 63 - __builtin_clzll(x); pos[n++] = wi * 64 + b; x &= ~(1ULL << b); } }
      std::fprintf(stderr, "DUMP k=%d v=%lld lam=", k, (long long)v);
      bool first = true;
      for (int j = 0; j < n; ++j) {
        const int lj = pos[j] - (n - 1 - j);
        if (lj <= 0) break;
        std::fprintf(stderr, "%s%d", first ? "" : ",", lj); first = false; }
      std::fputc('\n', stderr); }
    if (acc) { acc->add_serial(cur, v); return; }
    const int s = Accum<W, C>::shard_of(key_hash(cur));
    auto& buf = bufs[s];
    buf.push_back({cur, v});
    if (buf.size() >= FLUSH_N) flush_shard(s); }

  void flush_shard(int s) {
    auto& buf = bufs[s];
    if (buf.empty()) return;
    if (g_verbose) g_emissions.fetch_add(buf.size(), std::memory_order_relaxed);
    gacc->lock(s);
    gacc->maps[s].add_batch(buf.data(), buf.size());
    gacc->unlock(s);
    buf.clear(); }

  void flush_all() {
    if (!gacc) return;
    for (int s = 0; s < Accum<W, C>::SH; ++s) flush_shard(s); }

  void dfs(int i, int rem) {
    if (rem == 0) { emit(); return; }
    if (nsrc - i < rem) return;
    dfs(i + 1, rem);                           // skip source i
    const int p = srcPos[i];
    for (int t = 0; t < nv; ++t) {
      if (remc[t] == 0) continue;
      const int tgt = p + k * val[t];
      if (tgt > maxbit) continue;
      if (key_bit(cur, tgt)) continue;
      const int cross = key_between(cur, p, tgt);
      key_clear(cur, p); key_set(cur, tgt);
      remc[t]--; sign ^= (cross & 1);
      dfs(i + 1, rem - 1);
      remc[t]++; sign ^= (cross & 1);
      key_set(cur, p); key_clear(cur, tgt); } }

  // Fast path for single-valued mu = (v^c): every move has the same stride
  // s = k*v.  Occupied slots split into maximal stride-s chains ("runs");
  // only a suffix of a run can move, and shifting a suffix of t particles is
  // exactly {clear bit top-(t-1)s, set bit top+s} (interior members land on
  // each other's slots).  Runs are independent: top+s is free by maximality
  // and belongs to no other run's residue class.  The total sign is
  //   sum_q #occupied(q, q+s)  +  #{chosen pairs with distance < s}   (mod 2)
  // where the first sum is over chosen sources q in the ORIGINAL config.
  Key<W> orig;
  int stride;
  int runTop[MAX_SLOTS], runLen[MAX_SLOTS], runCapSuf[MAX_SLOTS + 1];
  int nrun;
  // chosen[] holds the moved sources of one DFS path; its length reaches
  // m.total = the monomial's part count (= n for a single-column inner
  // e_n = s_{(1^n)}), bounded by the bead count.  Must be MAX_SLOTS, not a
  // smaller fixed bound (chosen[16] once silently corrupted e_n, n >= 17).
  int chosen[MAX_SLOTS], nchosen;
  int sumStatic;
  int expectRun[2 * MAX_SLOTS];
  uint32_t expectGen[2 * MAX_SLOTS] = {};
  uint32_t curGen = 0;

  inline void emit_run() {
    int close = 0;
    for (int i = 0; i < nchosen; ++i)
      for (int j = i + 1; j < nchosen; ++j) {
        const int d = chosen[i] - chosen[j];
        close += (d < stride && d > -stride); }
    sign = (sumStatic + close) & 1;
    emit(); }

  void run_dfs(int i, int rem) {
    if (rem == 0) { emit_run(); return; }
    if (i == nrun || runCapSuf[i] < rem) return;
    run_dfs(i + 1, rem);                       // t = 0
    const int top = runTop[i];
    const int s = stride;
    if (top + s > maxbit) return;
    const int tmax = std::min(runLen[i], rem);
    for (int t = 1; t <= tmax; ++t) {
      const int src = top - (t - 1) * s;
      if (t == 1) { key_set(cur, top + s); key_clear(cur, top); }
      else        { key_set(cur, src + s); key_clear(cur, src); }
      assert(nchosen < MAX_SLOTS);
      chosen[nchosen++] = src;
      sumStatic += key_between(orig, src, src + s);
      run_dfs(i + 1, rem - t); }
    // undo: restore run bits, drop the shifted top, pop chosen sources
    key_clear(cur, top + s);
    key_set(cur, top - (tmax - 1) * s);
    for (int t = tmax; t >= 1; --t) {
      const int src = top - (t - 1) * s;
      sumStatic -= key_between(orig, src, src + s);
      --nchosen; } }
};

template <int W, class C>
static void run_term(PassCtx<W, C>& ctx, const Key<W>& key, C cin, int sigma,
                     const std::vector<Mono>& monos) {
  // all source positions, descending
  int allPos[MAX_SLOTS];
  int nall = 0;
  for (int wi = W - 1; wi >= 0; --wi) {
    uint64_t x = key.w[wi];
    while (x) {
      const int b = 63 - __builtin_clzll(x);
      assert(nall < MAX_SLOTS);
      allPos[nall++] = wi * 64 + b;
      x &= ~(1ULL << b); } }
  for (const Mono& m : monos) {
    ctx.cur = key;
    ctx.sign = 0;
    ctx.base = mulc(cin, C(sigma * m.K));
    if (m.nv == 1) {
      // run-level fast path: group positions into maximal stride-s chains
      const int s = ctx.k * m.val[0];
      ctx.stride = s;
      ctx.orig = key;
      ctx.nchosen = 0;
      ctx.sumStatic = 0;
      ctx.nrun = 0;
      ++ctx.curGen;
      for (int i = 0; i < nall; ++i) {
        const int p = allPos[i];
        int r;
        if (ctx.expectGen[p] == ctx.curGen) {
          r = ctx.expectRun[p];
          ctx.runLen[r]++; }
        else {
          r = ctx.nrun++;
          ctx.runTop[r] = p;
          ctx.runLen[r] = 1; }
        if (p >= s) {
          ctx.expectRun[p - s] = r;
          ctx.expectGen[p - s] = ctx.curGen; } }
      assert(ctx.nrun <= MAX_SLOTS);
      ctx.runCapSuf[ctx.nrun] = 0;
      for (int r = ctx.nrun - 1; r >= 0; --r)
        ctx.runCapSuf[r] = ctx.runCapSuf[r + 1] +
                           (ctx.runTop[r] + s <= ctx.maxbit ? ctx.runLen[r] : 0);
      ctx.run_dfs(0, m.total);
      continue; }
    ctx.nv = m.nv;
    for (int t = 0; t < m.nv; ++t) { ctx.val[t] = m.val[t]; ctx.remc[t] = m.cnt[t]; }
    // candidate filter: p can ever move iff for some value v the slot p+k*v is
    // free, or occupied by a particle that can itself move (upward chains).
    // Computed descending, so cand[] for higher slots is already known.
    bool cand[2 * MAX_SLOTS];
    ctx.nsrc = 0;
    for (int i = 0; i < nall; ++i) {
      const int p = allPos[i];
      bool ok = false;
      for (int t = 0; t < m.nv && !ok; ++t) {
        const int tgt = p + ctx.k * m.val[t];
        if (tgt > ctx.maxbit) continue;
        ok = !key_bit(key, tgt) || cand[tgt]; }
      cand[p] = ok;
      if (ok) { assert(ctx.nsrc < MAX_SLOTS); ctx.srcPos[ctx.nsrc++] = p; } }
    ctx.dfs(0, m.total); } }


template <int W, class C>
static void pass(const List<W, C>& in, int k, int sigma,
                 const std::vector<Mono>& monos, int maxbit, int lenZeroFloor,
                 Accum<W, C>& acc) {
  const double tstart = g_verbose ? now_s() : 0.0;
  const size_t nin = in.size();
  long mass = 0;
  for (const Mono& m : monos) mass += m.K;
  const bool par = nin >= 16 || nin * (size_t)mass >= 512;
  if (!par) {
    PassCtx<W, C> ctx;
    ctx.k = k; ctx.maxbit = maxbit; ctx.lenZeroFloor = lenZeroFloor; ctx.acc = &acc;
    for (size_t i = 0; i < nin; ++i)
      run_term(ctx, in.keys[i], in.coefs[i], sigma, monos);
    if (g_verbose)
      std::fprintf(stderr, "  pass k=%d |in|=%zu serial %.3fs acc=%zu\n",
                   k, nin, now_s() - tstart, acc.total_cnt());
    return; }
  const int nthr = (nin >= 65536) ? g_threads_big : g_threads_small;
#pragma omp parallel num_threads(nthr)
  {
    std::vector<typename Map<W, C>::Entry> bufs[Accum<W, C>::SH];
    PassCtx<W, C> ctx;
    ctx.k = k; ctx.maxbit = maxbit; ctx.lenZeroFloor = lenZeroFloor;
    ctx.acc = nullptr; ctx.bufs = bufs; ctx.gacc = &acc;
#pragma omp for schedule(guided, 1)
    for (size_t i = 0; i < nin; ++i)
      run_term(ctx, in.keys[i], in.coefs[i], sigma, monos);
    ctx.flush_all();
  }
  if (g_verbose)
    std::fprintf(stderr, "  pass k=%d |in|=%zu par    %.3fs acc=%zu emit=%llu\n",
                 k, nin, now_s() - tstart, acc.total_cnt(),
                 (unsigned long long)g_emissions.exchange(0));
}

// add w * list into accumulator
template <int W, class C>
static void add_scaled(const List<W, C>& l, C wgt, Accum<W, C>& acc) {
  for (size_t i = 0; i < l.size(); ++i)
    acc.add_serial(l.keys[i], mulc(l.coefs[i], wgt)); }

// ----------------------------------------------------------------- the engine

struct Plan {
  Part A;                 // effective outer
  Part G;                 // effective inner
  bool conjOut;           // conjugate output partitions
  int N;                  // beta slots
  int maxbit;             // highest usable bit
  int words;              // ceil((maxbit+1)/64)
  // output-shape filters (-1 = unbounded).  User bounds apply to the FINAL
  // partition nu; internal bounds are swapped under conjOut and drive engine
  // pruning.  len/first-part are monotone under the passes, so pruning is exact.
  int maxLenUser  = -1;   // drop nu with len(nu)  > maxLenUser
  int maxPartUser = -1;   // drop nu with nu[0]    > maxPartUser
  // internal length pruning: len(key) = N - key_lowest_zero(key), so the cap
  // len <= maxLen becomes key_lowest_zero >= N - maxLen.  lenZeroFloor is that
  // threshold; 0 disables (no key has len > N).
  int lenZeroFloor = 0;
};

static Plan make_plan(const Part& a, const Part& b,
                      int maxLenUser = -1, int maxPartUser = -1) {
  const Part bc = conj_part(b);
  const Part ac = conj_part(a);
  const int n = psum(a) * psum(b);

  auto mass = [](const Part& g) {
    long m = 0;
    for (const Mono& mo : monomials_of(g)) m += mo.K;
    return m;
  };
  const long mb = mass(b), mbc = mass(bc);
  bool flip = mbc < mb;
  if (const char* fo = std::getenv("PLETHY_ORIENT")) {   // testing override
    if (std::strcmp(fo, "direct") == 0) flip = false;
    if (std::strcmp(fo, "flip") == 0)   flip = true; }
  if (g_verbose)
    std::fprintf(stderr, "plan: mass(b)=%ld mass(b')=%ld flip=%d\n", mb, mbc, (int)flip);

  Plan pl;
  pl.G = flip ? bc : b;
  pl.A = (flip && (psum(b) % 2 == 1)) ? ac : a;
  pl.conjOut = flip;
  const int mA = psum(pl.A);
  const int lG = (int)pl.G.size();
  const int g1 = pl.G.empty() ? 0 : pl.G[0];
  pl.N = std::min(n, mA * lG);
  const int partBound = std::min(n, mA * g1);
  pl.maxbit = partBound + pl.N - 1;

  // Filters.  conjOut conjugates the final partition, which swaps length and
  // first-part; map the user's (len, part) bounds into internal orientation.
  pl.maxLenUser  = maxLenUser;
  pl.maxPartUser = maxPartUser;
  const int intLen  = pl.conjOut ? maxPartUser : maxLenUser;
  const int intPart = pl.conjOut ? maxLenUser  : maxPartUser;
  if (intLen >= 0)    // length cap -> floor on the lowest-zero bit position
    pl.lenZeroFloor = std::max(0, pl.N - intLen);
  if (intPart >= 0)   // first-part cap folds straight into the bit budget
    pl.maxbit = std::min(pl.maxbit, intPart + pl.N - 1);

  pl.words = (pl.maxbit + 64) / 64;
  return pl;
}

template <int W, class C>
struct Engine {
  const Plan& pl;
  std::vector<Mono> monos;

  explicit Engine(const Plan& p) : pl(p), monos(monomials_of(p.G)) {}

  Key<W> vacuum_key() const {
    Key<W> k;
    for (int i = 0; i < W; ++i) k.w[i] = 0;
    for (int i = 0; i < pl.N; ++i) key_set(k, i);
    return k; }

  List<W, C> vacuum() const {
    List<W, C> l;
    l.keys.push_back(vacuum_key());
    l.coefs.push_back(C(1));
    return l; }

  // Newton chain: returns T_m = h_m[f] (eFlavor=false) or e_m[f] (true)
  List<W, C> chain(int m, bool eFlavor) {
    std::vector<List<W, C>> T(m + 1);
    T[0] = vacuum();
    for (int j = 1; j <= m; ++j) {
      Accum<W, C> acc;
      acc.reserve_total(4 * T[j - 1].size());
      for (int i = 1; i <= j; ++i) {
        const int sigma = eFlavor ? ((i % 2 == 1) ? 1 : -1) : 1;
        pass(T[j - i], i, sigma, monos, pl.maxbit, pl.lenZeroFloor, acc); }
      T[j] = acc.finalize(j); }
    return std::move(T[m]); }

  // chi-route for general outer A.  rho is built with parts ASCENDING so that
  // the expensive p_1-chains over the largest lists are shared across all rho
  // containing many small parts.
  List<W, C> chi_route() {
    const int m = psum(pl.A);
    const int64_t mfact = factorial(m);
    Accum<W, C> total;
    Part rho;
    chi_rec_tree(vacuum(), 1, m, rho, mfact, total);
    return total.finalize(mfact); }

  void chi_rec_tree(const List<W, C>& cur, int minPart, int remaining, Part& rho,
                    int64_t mfact, Accum<W, C>& total) {
    for (int q = minPart; q <= remaining; ++q) {
      if (remaining - q != 0 && remaining - q < q) continue;  // can't extend ascending
      rho.push_back(q);
      Accum<W, C> acc;
      acc.reserve_total(4 * cur.size());
      pass(cur, q, 1, monos, pl.maxbit, pl.lenZeroFloor, acc);
      List<W, C> child = acc.finalize(1);
      if (remaining - q == 0) {
        Part rhoDesc(rho.rbegin(), rho.rend());
        const int64_t z = z_of(rhoDesc);
        assert(mfact % z == 0);
        const int64_t w = (mfact / z) * chi_char(pl.A, rhoDesc);
        if (w != 0) add_scaled(child, C(w), total); }
      else
        chi_rec_tree(child, q, remaining - q, rho, mfact, total);
      rho.pop_back(); } }

  List<W, C> run() {
    if (pl.A.size() == 1) return chain(pl.A[0], false);          // h_m[f]
    if (pl.A[0] == 1)     return chain((int)pl.A.size(), true);  // e_m[f]
    return chi_route(); }

  // decode a key into a partition (descending parts, zeros trimmed)
  Part decode(const Key<W>& k) const {
    Part lam;
    int i = 1;   // 1-based row index
    for (int wi = W - 1; wi >= 0; --wi) {
      uint64_t x = k.w[wi];
      while (x) {
        const int b = 63 - __builtin_clzll(x);
        x &= ~(1ULL << b);
        const int part = (wi * 64 + b) - (pl.N - i);
        assert(part >= 0);
        if (part == 0) return lam;     // all further parts are 0
        lam.push_back(part);
        ++i; } }
    return lam; }

};

// canonical term hash (order-independent; see paper)
static void term_hash(const uint8_t* parts, uint32_t k, bool neg,
                      const uint8_t* mag, uint32_t magLen, uint8_t out[16]) {
  Sha256 s;
  uint8_t tmp[4];
  auto u32le = [&](uint32_t v) {
    tmp[0] = (uint8_t)v; tmp[1] = (uint8_t)(v >> 8); tmp[2] = (uint8_t)(v >> 16); tmp[3] = (uint8_t)(v >> 24);
    s.update(tmp, 4); };
  u32le(k);
  for (uint32_t i = 0; i < k; ++i) u32le(parts[i]);
  const uint8_t sg = neg ? 1 : 0;
  s.update(&sg, 1);
  u32le(magLen);
  s.update(mag, magLen);
  s.final16(out); }

// ------------------------------------------------------------------- parsing
// Input partition formats, auto-detected per argument (plethy-compatible):
//   LaTeX / comma  s_{2,1}  s_{1^7}  s_{2,1^4}  s_4  s_{}  s_{1^{10}}  {2,1}  2,1
//   Symmetrica     21  1^7  2111  4  (empty)  A-F = parts 10..15  |NN = part>=16
//   Raw (--raw)    2,1   comma-decimal, non-increasing;  "" = empty partition
// All parsers throw std::runtime_error (caught in main) on malformed input and
// return a weakly-decreasing Part (possibly empty).

// Auto-detect: arguments carrying s/{/, use the LaTeX/comma parser; bare
// tokens are Symmetrica shorthand (after '^' expansion).
static Part parse_arg(const std::string& a) {
  if (a.find_first_of("s{,") != std::string::npos) return parse_latex(a);
  return parse_symm(expand_symm(a)); }

// --------------------------------------------------------------------- output

struct FlatTerms {              // structure-of-arrays answer storage
  std::vector<uint8_t> parts;   // arena: parts (descending) of every term
  std::vector<uint32_t> off;    // term i occupies parts[off[i] .. off[i+1])
  std::vector<i128> coef;
  size_t size() const { return coef.size(); }
};

// ----------------------------------------------------------------------- main

template <int W, class C>
static bool solve_w(const Plan& pl, FlatTerms& ft) {
  g_overflow.store(false);
  Engine<W, C> eng(pl);
  List<W, C> ans = eng.run();
  if (g_overflow.load()) return false;
  const size_t nt = ans.size();
  ft.off.assign(1, 0);
  ft.parts.clear();
  ft.coef.clear();
  ft.off.reserve(nt + 1);
  ft.coef.reserve(nt);
  ft.parts.reserve(nt * 8);
  Part lam;
  for (size_t i = 0; i < nt; ++i) {
    lam = eng.decode(ans.keys[i]);
    if (pl.conjOut) lam = conj_part(lam);
    // authoritative filter on the final nu (internal pruning makes this exact;
    // kept as a correctness guard so output == filter(unbounded) to the term).
    if (pl.maxLenUser  >= 0 && (int)lam.size() > pl.maxLenUser)          continue;
    if (pl.maxPartUser >= 0 && !lam.empty() && lam[0] > pl.maxPartUser)  continue;
    const i128 c = (i128)ans.coefs[i];
    if (c < 0) {
      std::fprintf(stderr, "plethy: FATAL negative coefficient (Schur-positivity violated)\n");
      std::exit(3);
    }
    for (int p : lam) {
      assert(p > 0 && p < 256);
      ft.parts.push_back((uint8_t)p);
    }
    ft.off.push_back((uint32_t)ft.parts.size());
    ft.coef.push_back(c);
  }
  return true;
}

template <int W>
static void solve_dispatch_c(const Plan& pl, FlatTerms& ft) {
  const bool force128 = std::getenv("PLETHY_FORCE_I128") != nullptr;
  if (!force128 && solve_w<W, int64_t>(pl, ft)) return;
  if (!force128)
    std::fprintf(stderr, "plethy: int64 overflow, retrying with int128\n");
  if (solve_w<W, i128>(pl, ft)) return;
  std::fprintf(stderr, "plethy: FATAL int128 overflow\n");
  std::exit(3);
}


// --------------------------------------------------------------- output formats
// Contest (default on a pipe) is the harness contract: n_terms / term lines /
// hash / seconds.  Latex (default on a tty), Symm, and Raw mirror plethy.

enum class Fmt { Contest, Latex, Symm, Raw };

static std::string coef_dec(i128 c) {
  const bool neg = c < 0;
  unsigned __int128 m = neg ? (unsigned __int128)(-c) : (unsigned __int128)c;
  char tmp[48]; int k = 0;
  do { tmp[k++] = (char)('0' + (int)(m % 10)); m /= 10; } while (m);
  std::string s;
  if (neg) s += '-';
  while (k) s += tmp[--k];
  return s; }

// Symmetrica term text: digits, hex A-F (10..15), |NN (>=16).
static std::string fmt_symm(const int* p, int k) {
  std::string s;
  for (int i = 0; i < k; ++i) {
    const int v = p[i];
    if (v < 10) s += (char)('0' + v);
    else if (v < 16) s += (char)('A' + v - 10);
    else { s += '|'; s += std::to_string(v); } }
  return s; }

// LaTeX bare inner of s_{...}: comma-separated, runs of >=3 collapsed to v^run.
static std::string fmt_latex(const int* p, int k) {
  std::string out;
  for (int i = 0; i < k; ) {
    int j = i;
    while (j < k && p[j] == p[i]) ++j;
    const int run = j - i;
    if (!out.empty()) out += ',';
    out += std::to_string(p[i]);
    if (run >= 3) { out += '^'; out += std::to_string(run); }
    else for (int t = 1; t < run; ++t) { out += ','; out += std::to_string(p[i]); }
    i = j; }
  return out; }

int main(int argc, char** argv) {
  static const char* USAGE =
    "usage: plethy [flags] OUTER INNER | plethy [flags] 'OUTER[INNER]'\n"
    "\n"
    "Partition input is auto-detected per argument:\n"
    "  LaTeX/comma  s_{2,1}  s_{1^7}  s_{2,1^4}  s_4  s_{}  s_{1^{10}}  {2,1}  2,1\n"
    "  Symmetrica   21  1^7  2111  4  (empty)   A-F = 10..15   |NN = part>=16\n"
    "  Raw (--raw)  2,1  comma-decimal, non-increasing;  \"\" = empty partition\n"
    "\n"
    "Flags:\n"
    "  -l N, --max-length=N   drop output terms with len(nu) > N\n"
    "  -p M, --max-part=M     drop output terms with nu[0]   > M\n"
    "  -q                     suppress the expanded result (keep header/hash)\n"
    "  -H, --hash128          show the 128-bit canonical hash\n"
    "  --format=latex|symm|raw|contest   override output format\n"
    "  --raw                  machine I/O: comma-decimal in, 'COEF k p1..pk' out\n"
    "  --help, -?             print this help\n"
    "\n"
    "Default output: latex on a tty, the contest format (n_terms/hash/seconds)\n"
    "on a pipe.  --raw and --format= are mutually exclusive.\n";
  int maxLength = -1, maxPart = -1;
  bool quiet = false, hash128 = false, raw = false;
  std::optional<Fmt> fmtOverride;
  std::vector<const char*> pos;
  for (int ai = 1; ai < argc; ++ai) {
    const std::string a = argv[ai];
    // parse an int from the inline --flag=VALUE (eq != null) or the next argv.
    auto take_int = [&](int& dst, const char* eq) -> bool {
      const char* v = eq ? eq : (ai + 1 < argc ? argv[++ai] : nullptr);
      if (!v) return false;
      const char* const vend = v + std::strlen(v);
      int n;
      const auto [ptr, ec] = std::from_chars(v, vend, n);
      if (ec != std::errc{} || ptr != vend || n < 0) return false;
      dst = n;
      return true; };
    bool ok = true;
    if      (a == "-l")                          ok = take_int(maxLength, nullptr);
    else if (a.rfind("--max-length=", 0) == 0)   ok = take_int(maxLength, argv[ai] + 13);
    else if (a == "-p")                          ok = take_int(maxPart, nullptr);
    else if (a.rfind("--max-part=", 0) == 0)     ok = take_int(maxPart, argv[ai] + 11);
    else if (a == "--raw")                       raw = true;
    else if (a == "--hash128")                   hash128 = true;
    else if (a.rfind("--format=", 0) == 0) {
      const std::string f = a.substr(9);
      if      (f == "latex")   fmtOverride = Fmt::Latex;
      else if (f == "symm")    fmtOverride = Fmt::Symm;
      else if (f == "raw")     fmtOverride = Fmt::Raw;
      else if (f == "contest") fmtOverride = Fmt::Contest;
      else ok = false; }
    else if (a == "--help" || a == "-?") { std::fputs(USAGE, stdout); return 0; }
    else if (a.size() >= 2 && a[0] == '-' && a[1] != '-') {
      for (size_t c = 1; c < a.size() && ok; ++c)        // combined short flags
        switch (a[c]) {
          case 'q': quiet = true; break;
          case 'H': hash128 = true; break;
          case '?': std::fputs(USAGE, stdout); return 0;
          default:  ok = false; } }
    else pos.push_back(argv[ai]);
    if (!ok) { std::fputs(USAGE, stderr); return 2; } }

  if (raw && fmtOverride && *fmtOverride != Fmt::Raw) {
    std::fprintf(stderr, "plethy: --raw and --format= are mutually exclusive\n");
    return 2; }
  const Fmt fmt = raw            ? Fmt::Raw
                : fmtOverride     ? *fmtOverride
                : isatty(STDOUT_FILENO) ? Fmt::Latex
                                        : Fmt::Contest;

  std::string outerStr, innerStr;
  if (pos.size() == 1) {
    const std::string s(pos[0]);
    const size_t lb = s.find('[');
    const size_t rb = s.rfind(']');
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) { std::fputs(USAGE, stderr); return 2; }
    outerStr = s.substr(0, lb);
    innerStr = s.substr(lb + 1, rb - lb - 1); }
  else if (pos.size() == 2) { outerStr = pos[0]; innerStr = pos[1]; }
  else { std::fputs(USAGE, stderr); return 2; }

  Part a, b;
  try {
    a = raw ? parse_raw(outerStr) : parse_arg(outerStr);
    b = raw ? parse_raw(innerStr) : parse_arg(innerStr); }
  catch (const std::exception& e) {
    std::fprintf(stderr, "plethy: %s\n", e.what());
    return 2; }

  g_verbose = std::getenv("PLETHY_VERBOSE") != nullptr;
  g_dump    = std::getenv("PLETHY_DUMP")    != nullptr;

  // Fail cleanly (one message, exit 3) instead of a core-dumping std::terminate
  // when an allocation fails -- including in OpenMP worker threads, where a
  // thrown bad_alloc cannot be caught in main.  Runs on any operator-new
  // failure, e.g. once the PLETHY_MEM_LIMIT_GB cap below is hit.
  std::set_new_handler([] {
    std::fputs("plethy: out of memory (problem too large; see PLETHY_MEM_LIMIT_GB)\n", stderr);
    std::fflush(stderr);
    std::_Exit(3); });

  // Optional address-space cap: PLETHY_MEM_LIMIT_GB=N makes a runaway plethysm
  // (e.g. a huge inner) hit the new-handler above instead of OOM-killing the host.
  set_mem_limit_from_env("PLETHY_MEM_LIMIT_GB");
  {
    const int hw = omp_get_max_threads();
    g_threads_small = std::min(64, hw);
    g_threads_big = std::min(192, hw);
    if (const char* e = std::getenv("PLETHY_THREADS")) {
      g_threads_small = g_threads_big = std::min(std::atoi(e), hw);
    }
    omp_set_dynamic(0);
  }

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  FlatTerms ft;
  if (a.empty() || b.empty()) {
    // |a|*|b| = 0 => degree 0 => the result is the empty partition with coef 1
    // (s_{}[g] = 1 = f[s_{}]); matches the reference for all empty inputs.
    ft.off.assign(1, 0);
    ft.off.push_back(0);
    ft.coef.push_back(1); }
  else {
    const Plan pl = make_plan(a, b, maxLength, maxPart);
    // Out-of-envelope guard: the fast-path arrays hold MAX_SLOTS beads.  This
    // is implied by words <= 4 (maxbit <= 255 => N <= 256), but we check it
    // explicitly so a future change can never silently overrun a buffer.
    if (pl.N > MAX_SLOTS) {
      std::fprintf(stderr, "plethy: degree too large (N=%d slots > %d)\n", pl.N, MAX_SLOTS);
      return 2; }
    // Every output term has weight |a|*|b|; a partition of n needs len*part >= n,
    // so if both bounds are set and len_max*part_max < n the result is empty.
    const long long n = (long long)psum(a) * (long long)psum(b);
    const bool empty_by_bounds = maxLength >= 0 && maxPart >= 0 &&
                                 (long long)maxLength * (long long)maxPart < n;
    if (empty_by_bounds) ft.off.assign(1, 0);
    else switch (pl.words) {
      case 1: solve_dispatch_c<1>(pl, ft); break;
      case 2: solve_dispatch_c<2>(pl, ft); break;
      case 3: solve_dispatch_c<3>(pl, ft); break;
      case 4: solve_dispatch_c<4>(pl, ft); break;
      default:
        std::fprintf(stderr, "plethy: degree too large (words=%d)\n", pl.words);
        return 2; } }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  const double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

  const size_t nt = ft.size();

  // sort terms descending-lex (cosmetic; the hash is order-independent)
  std::vector<uint32_t> order(nt);
  for (size_t i = 0; i < nt; ++i) order[i] = (uint32_t)i;
  std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
    const uint8_t* px = ft.parts.data() + ft.off[x];
    const uint8_t* py = ft.parts.data() + ft.off[y];
    const uint32_t kx = ft.off[x + 1] - ft.off[x];
    const uint32_t ky = ft.off[y + 1] - ft.off[y];
    const uint32_t k = std::min(kx, ky);
    for (uint32_t i = 0; i < k; ++i)
      if (px[i] != py[i]) return px[i] > py[i];
    return kx > ky;
  });

  // Only do the work each format actually consumes: the 128-bit hash is shown
  // by the contest format and by -H; the comma-format term lines only by the
  // contest format (and not under -q).  --raw / latex / symm without -H skip
  // both -- which is the bindings' compute() path.
  const bool want_hash  = fmt == Fmt::Contest || hash128;
  const bool want_lines = fmt == Fmt::Contest && !quiet;
  uint8_t acc[16] = {0};
  std::vector<char> lines;          // assembled term lines (contest format)
  if (want_hash || want_lines) {
    std::vector<uint64_t> lineOff;
    if (want_lines) {               // first pass: line lengths -> offsets
      lineOff.assign(nt + 1, 0);
      std::vector<uint32_t> lineLen(nt);
#pragma omp parallel for schedule(static) if (nt > 8192)
      for (size_t oi = 0; oi < nt; ++oi) {
        const uint32_t i = order[oi];
        unsigned __int128 m = (unsigned __int128)ft.coef[i];
        uint32_t len = 0;
        do { ++len; m /= 10; } while (m);
        for (uint32_t j = ft.off[i]; j < ft.off[i + 1]; ++j) {
          ++len;                                       // comma or leading space
          for (uint32_t v = ft.parts[j]; v; v /= 10) ++len; }
        if (ft.off[i] == ft.off[i + 1]) ++len;         // empty partition: just space
        lineLen[oi] = len + 1; }                        // newline
      for (size_t oi = 0; oi < nt; ++oi) lineOff[oi + 1] = lineOff[oi] + lineLen[oi];
      lines.resize(lineOff[nt]); }

#pragma omp parallel if (nt > 4096)
    {
      uint8_t local[16] = {0};
#pragma omp for schedule(static)
      for (size_t oi = 0; oi < nt; ++oi) {
        const uint32_t i = order[oi];
        const uint8_t* parts = ft.parts.data() + ft.off[i];
        const uint32_t k = ft.off[i + 1] - ft.off[i];
        if (want_hash) {                               // magnitude bytes (big-endian) + XOR
          unsigned __int128 m = (unsigned __int128)ft.coef[i];
          uint8_t rev[16]; uint32_t nb = 0;
          while (m) { rev[nb++] = (uint8_t)(m & 0xff); m >>= 8; }
          uint8_t mag[16];
          for (uint32_t j = 0; j < nb; ++j) mag[j] = rev[nb - 1 - j];
          uint8_t h[16];
          term_hash(parts, k, false, mag, nb, h);
          for (int j = 0; j < 16; ++j) local[j] ^= h[j]; }
        if (want_lines) {                              // format one contest line
          char* p = lines.data() + lineOff[oi];
          unsigned __int128 d = (unsigned __int128)ft.coef[i];
          char tmp[48]; int dk = 0;
          do { tmp[dk++] = (char)('0' + (int)(d % 10)); d /= 10; } while (d);
          while (dk) *p++ = tmp[--dk];
          for (uint32_t j = 0; j < k; ++j) { *p++ = j ? ',' : ' '; p = put_u32(p, parts[j]); }
          if (k == 0) *p++ = ' ';
          *p++ = '\n';
          assert(p == lines.data() + lineOff[oi + 1]); } }
      if (want_hash) {
#pragma omp critical
        for (int j = 0; j < 16; ++j) acc[j] ^= local[j]; } } }

  static const char* hexd = "0123456789abcdef";
  std::string hhex(32, '0');
  for (int j = 0; j < 16; ++j) { hhex[2 * j] = hexd[acc[j] >> 4]; hhex[2 * j + 1] = hexd[acc[j] & 15]; }

  if (fmt == Fmt::Contest) {
    const std::string head = std::format("n_terms {}\n", nt);
    std::fwrite(head.data(), 1, head.size(), stdout);
    if (!quiet) std::fwrite(lines.data(), 1, lines.size(), stdout);
    const std::string tail = std::format("hash {}\nseconds {:.6f}\n", hhex, secs);
    std::fwrite(tail.data(), 1, tail.size(), stdout); }
  else if (fmt == Fmt::Raw) {                  // machine I/O: batch into one write
    std::string out;
    if (hash128) { out += hhex; out += '\n'; }              // bare hash, before terms
    if (!quiet)
      for (size_t oi = 0; oi < nt; ++oi) {
        const uint32_t i = order[oi];
        const uint8_t* parts = ft.parts.data() + ft.off[i];
        const uint32_t k = ft.off[i + 1] - ft.off[i];
        out += coef_dec(ft.coef[i]); out += ' '; out += std::to_string(k);
        for (uint32_t j = 0; j < k; ++j) { out += ' '; out += std::to_string((int)parts[j]); }
        out += '\n'; }
    std::fwrite(out.data(), 1, out.size(), stdout); }
  else {                                        // human: Latex / Symm
    const std::string lhs = fmt == Fmt::Latex
      ? "s_{" + fmt_latex(a.data(), (int)a.size()) + "}[s_{" + fmt_latex(b.data(), (int)b.size()) + "}]"
      : fmt_symm(a.data(), (int)a.size()) + "[" + fmt_symm(b.data(), (int)b.size()) + "]";
    if (!quiet) {
      std::print("{} = ", lhs);
      if (nt == 0 && fmt == Fmt::Latex) std::print("0");
      bool first = true;
      for (size_t oi = 0; oi < nt; ++oi) {
        const uint32_t i = order[oi];
        const uint8_t* parts = ft.parts.data() + ft.off[i];
        const uint32_t k = ft.off[i + 1] - ft.off[i];
        const std::vector<int> pv(parts, parts + k);
        if (fmt == Fmt::Latex) {
          if (!first) std::print(" + ");
          first = false;
          if (ft.coef[i] != 1) std::print("{} ", coef_dec(ft.coef[i]));
          std::print("s_{{{}}}", fmt_latex(pv.data(), (int)k)); }
        else {
          if (!first) std::print("  ");
          first = false;
          std::print("{} {}", coef_dec(ft.coef[i]), fmt_symm(pv.data(), (int)k)); } }
      if (fmt == Fmt::Symm) std::print("  ");
      std::println(""); }
    if (hash128) std::println("hash128({}) = {}", lhs, hhex); }
  std::fflush(stdout);
  return 0; }
