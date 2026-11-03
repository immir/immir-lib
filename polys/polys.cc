#if 0
set -x
g++ -O2 -g -std=gnu++23 -march=native -Wall -Wextra -Werror -o ${0%.*} $0 && ./${0%.*} $*
exit
#endif

/*
  polys.cc
  --------

  Code for critical points of multiple polynomials using simd.  Do
  Laguerre's method on the derivative in each lane, divide out the
  roots found, repeat until done, then polish against the original f'.

  f real-rooted => f' real-rooted (Rolle), and Laguerre converges
  globally (cubically near simple roots) on real-rooted polynomials,
  so any real starting point works; 0 is the default.

  Immir, 2025

*/


#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <print>
#include <format>
#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <random>

using std::vector, std::cout, std::ostream, std::string, std::print, std::println;
using std::min, std::max;

#define append push_back

#define N 4

using vec = double __attribute__((__vector_size__(N*sizeof(double))));
using polys = vector<vec>;
const double ε = 1e-15;
bool verbose = false;

template<typename T>
ostream& operator<<(ostream& os, const vector<T>& a) {
  os << "[ "; string c=""; for (auto &x: a) { os << c << x; c = ", "; }
  return os << "]"; }

ostream& operator<<(ostream& os, const vec& a) {
  os << "[ "; string c=""; for (int i=0; i<N; i++) { os << c << a[i]; c = ", "; }
  return os << "]"; }

// format/print template for our SIMD data type:
template <> struct std::formatter<vec> : std::formatter<string_view> {
  auto format(const vec& v, std::format_context& ctx) const {
    std::string temp;
    std::format_to(std::back_inserter(temp), "{} ", "{");
    for (int i = 0; i < N; i++)
      std::format_to(std::back_inserter(temp), "{}{}", i?", ":"", v[i]);
    std::format_to(std::back_inserter(temp), " {}", "}");
    return std::formatter<string_view>::format(temp, ctx); } };

void dump (polys f) {
  int d = f.size();
  for (int i = 0; i < N; i++) {
    bool first = true;
    if (i==0) print("[ "); else print("  ");
    for (int j = d-1; j >= 0; j--) {
      double c = f[j][i];
      if (c == 0) continue;
      if (c < 0) print(" - ");
      else if (!first) print(" + ");
      c = fabs(c);
      print("{:.8}", c);
      if (j == 1) print(" * x");
      else if (j > 1) print(" * x^{}", j);
      first = false; }
    if (i == N-1) println(" ];"); else println(","); } }

vec abs(vec x) {
  vec y;
  for (int i = 0; i < N; i++)
    y[i] = fabs(x[i]);
  return y; }

vec min(const vec &x, const vec &y) {
  vec z; for (int i = 0; i < N; i++) z[i] = min(x[i],y[i]);
  return z; }

vec max(const vec &x, const vec &y) {
  vec z; for (int i = 0; i < N; i++) z[i] = max(x[i],y[i]);
  return z; }

double max (const vec &x) {
  double m = x[0];
  for (int i = 1; i < N; i++) m = max(m, x[i]);
  return m; }

polys operator+ (const polys &f, const polys &g) {
  polys h; uint i;
  for (i = 0; i < min(f.size(),g.size()); i++) h.append(f[i] + g[i]);
  for (; i < f.size(); i++) h.append(f[i]);
  for (; i < g.size(); i++) h.append(g[i]);
  return h; }

polys operator- (const polys &f) {
  polys g = f; for (auto &x: g) x = -x; return g; }

polys operator- (const polys &f, const polys &g) {
  return f + (-g); }

polys operator+ (const polys &f, auto a) { polys g = f; g[0] += a; return g; }
polys operator+ (auto a, const polys &f) { polys g = f; g[0] += a; return g; }

polys operator- (const polys &f, auto a) { polys g = f; g[0] -= a; return g; }
polys operator- (auto a, const polys &f) { return -f + a; }

polys operator* (const polys &f, const polys &g) {
  int deg = (f.size()-1) + (g.size()-1);
  polys h; h.resize(deg+1);
  for (uint i = 0; i < f.size(); i++)
    for (uint j = 0; j < g.size(); j++)
      h[i+j] += f[i] * g[j];
  return h; }

polys operator* (auto a, const polys &f) {
  polys h = f;
  for (uint i = 0; i < f.size(); i++)
    h[i] *= a;
  return h; }

polys operator* (const polys &f, auto a) { return a*f; }

polys X() { return { vec{}, vec{}+1 }; }

int degree(const polys &f) { return (int) f.size()-1; }

polys shift(const polys &f, int d) {
  polys h;
  for (int i = 0; i < d; i++) h.append(vec{});
  for (auto v: f) h.append(v);
  return h; }

polys operator/ (const polys &f, const polys &g) {
  // assumes g divides f (or close enough), ignores remainder...
  /*

          f  f0 f1 f2 f3 f4   deg 4
          g  g0 g1 g2         deg 2   4-2 = 2

                   g0 g1 g2
                g0 g1 g2
             g0 g1 g2
   */
  polys h = f, q{};
  int df = degree(f), dg = degree(g);
  for (int i = 0; i <= df - dg; i++) {
    vec a = h[df - i] / g[dg];
    h = h - a * shift(g, df-dg-i);
    q.append(a); }
  // TODO: check h is *close* to zero?
  reverse(q.begin(), q.end());
  return q; }

polys derivative(const polys &f) {
  polys g; int d = degree(f);
  for (int i = 1; i <= d; i++)
    g.append(i * f[i]);
  return g; }

polys antiderivative(const polys &f) {
  polys g; int d = degree(f);
  for (int i = 0; i <= d; i++)
    g.append(f[i]/(i+1));
  g = shift(g, 1);
  return g; }

vec eval(polys f, vec x) {
  vec z{}, xx = vec{}+1;
  int d = f.size() - 1;
  for (int i = 0; i <= d; i++, xx *= x)
    z += xx * f[i];
  return z; }

struct horner3 { vec p, dp, ddp, abs_sum; };

// p(x), p'(x), p''(x) in a single Horner pass, plus sum |a_i| |x|^i
// (the scale of the rounding error in p)
horner3 eval_with_derivatives(const polys &f, const vec x) {
  assert(degree(f) >= 0);
  const vec ax = abs(x);
  vec p = f.back(), dp{}, ddp{}, abs_sum = abs(f.back());
  for (int i = degree(f) - 1; i >= 0; i--) {
    ddp     = ddp * x + dp;
    dp      = dp  * x + p;
    p       = p   * x + f[i];
    abs_sum = abs_sum * ax + abs(f[i]); }
  return { p, dp, 2 * ddp, abs_sum }; }

bool all_lanes(const auto mask) {
  bool ok = true;
  for (int i = 0; i < N; i++) ok &= mask[i] != 0;
  return ok; }

// Laguerre correction dz (so z_next = z - dz).  For real-rooted f the
// radicand (n-1)(nH - G^2) is >= 0 in exact arithmetic; clamp rounding.
// Lanes where |p(z)| is within the Horner rounding bound 2n eps sum|a_i||z|^i
// are a root to working precision and get dz = 0.
vec laguerre_step(const polys &f, const vec z) {
  const int n = degree(f);
  assert(n >= 1);
  const auto [p, dp, ddp, abs_sum] = eval_with_derivatives(f, z);
  const vec G = dp / p;
  const vec H = G * G - ddp / p;
  const vec radicand = (n - 1) * (n * H - G * G);
  vec denom;
  for (int i = 0; i < N; i++)
    denom[i] = G[i] + std::copysign(std::sqrt(std::max(0.0, radicand[i])), G[i]);
  return abs(p) <= 2 * n * DBL_EPSILON * abs_sum ? vec{} : n / denom; }

const int max_laguerre_iter = 64;
const double root_tol = 1e-14;

// iterate until every lane has |dz| <= tol (1 + |z|) (or is at the rounding
// floor, dz = 0); NaN lanes never pass
vec laguerre_root(const polys &f, vec z) {
  for (int iter = 0; iter < max_laguerre_iter; iter++) {
    const vec dz = laguerre_step(f, z);
    z -= dz;
    if (all_lanes(abs(dz) <= root_tol * (1 + abs(z))))
      return z; }
  std::println(stderr, "warning: Laguerre did not converge in {} iterations, z = {}",
               max_laguerre_iter, z);
  std::fflush(stderr);
  return z; }

vector<vec> critical_points(const polys &f, const vec z0) {
  // critical points of the N polys in f[] (one per lane), assumed
  // real-rooted, so Laguerre converges globally from any real z0.
  // Roots found on the deflated f' are polished against the original.
  const polys f1 = derivative(f);
  const polys x = X();
  vector<vec> roots;
  for (polys g = f1; degree(g) > 0; ) {
    const vec z = laguerre_root(g, z0);
    roots.append(z);
    g = g / (x - z); }
  assert((int) roots.size() == degree(f1));
  for (auto &z: roots)
    z = laguerre_root(f1, z);
  return roots; }

vector<vec> critical_points(const polys &f) {
  return critical_points(f, vec{}); }


/*
  Special case: f given by its roots, f = prod (x - r_i), all real.

  After sorting, Rolle puts exactly one critical point strictly inside
  each gap (r_k, r_{k+1}); it is the zero of the log-derivative

      g(x) = f'/f = sum_i 1/(x - r_i),

  which is strictly decreasing on each gap (g' = -sum 1/(x - r_i)^2 < 0).
  So every gap is solved independently by Newton safeguarded with the
  bracket: no coefficients, no deflation, no error carried between roots.

  Start point: keep the two poles of the gap exactly and freeze the rest
  of g at the midpoint m, C = sum_{i != k,k+1} 1/(m - r_i).  Then
  1/(x-a) + 1/(x-b) + C = 0 with x = a + tau, w = b - a gives
  C tau^2 + (2 - C w) tau - w = 0, whose root in (0, w) is (stably)

      tau = 2w / (2 - C w + sqrt(4 + C^2 w^2)).
*/

template <int n> using roots_t = std::array<vec, n>;

// per-lane sort by odd-even transposition network: branch-free, lanes independent
template <int n>
roots_t<n> sorted(roots_t<n> r) {
  for (int pass = 0; pass < n; pass++)
    for (int i = pass & 1; i + 1 < n; i += 2) {
      const vec lo = min(r[i], r[i+1]), hi = max(r[i], r[i+1]);
      r[i] = lo, r[i+1] = hi; }
  return r; }

template <int n>
struct log_derivative_t { vec g, dg; };

template <int n>
log_derivative_t<n> log_derivative(const roots_t<n> &r, const vec x) {
  vec g{}, dg{};
  for (const vec ri: r) {
    const vec t = 1.0 / (x - ri);
    g += t;
    dg -= t * t; }
  return { g, dg }; }

template <int n>
vec two_pole_start(const roots_t<n> &r, const int k) {
  const vec a = r[k], b = r[k+1], w = b - a, m = (a + b) / 2;
  vec C{};
  for (int i = 0; i < n; i++)
    if (i != k && i != k+1) C += 1.0 / (m - r[i]);
  const vec Cw = C * w, disc = 4 + Cw * Cw;
  vec root;
  for (int j = 0; j < N; j++) root[j] = std::sqrt(disc[j]);
  return a + 2 * w / (2 - Cw + root); }

template <int n>
struct critical_points_result { std::array<vec, n-1> points; int iterations; };

// critical points of prod (x - r_i), ascending in each lane; a double root
// (zero-width gap) is itself the critical point
template <int n>
critical_points_result<n> critical_points_from_roots(const roots_t<n> &roots) {
  static_assert(n >= 2);
  const roots_t<n> r = sorted<n>(roots);
  std::array<vec, n-1> lo, hi, x;
  for (int k = 0; k < n-1; k++) {
    lo[k] = r[k], hi[k] = r[k+1];
    assert(all_lanes(lo[k] <= hi[k]));
    x[k] = lo[k] == hi[k] ? lo[k] : two_pole_start<n>(r, k); }

  for (int iter = 1; iter <= max_laguerre_iter; iter++) {
    bool done = true;
    for (int k = 0; k < n-1; k++) {
      const auto [g, dg] = log_derivative<n>(r, x[k]);
      lo[k] = g > 0 ? x[k] : lo[k];
      hi[k] = g < 0 ? x[k] : hi[k];
      const vec newton = x[k] - g / dg;
      const vec stepped = (newton >= lo[k]) & (newton <= hi[k]) ? newton : (lo[k] + hi[k]) / 2;
      const vec next = (g == 0) | (lo[k] == hi[k]) ? x[k] : stepped;
      done &= all_lanes(abs(next - x[k]) <= root_tol * (1 + abs(next)));
      x[k] = next; }
    if (done) return { x, iter }; }

  std::println(stderr, "warning: critical_points_from_roots did not converge in {} iterations",
               max_laguerre_iter);
  std::fflush(stderr);
  return { x, max_laguerre_iter }; }

// f(c) = prod (c - r_i), one multiply per root
template <int n>
vec eval_from_roots(const roots_t<n> &r, const vec c) {
  vec v = vec{} + 1;
  for (const vec ri: r) v *= c - ri;
  return v; }


/*
  Jackpot search.

  Lane i holds a polynomial f_i in its own coordinate x_i, and the norm on
  the fixed box B = prod [lo_i, hi_i] is prod_i f_i(x_i).  It separates:

      max_B |norm| = prod_i M_i,   M_i = max_{[lo_i, hi_i]} |f_i|,

  and a jackpot is prod_i M_i < 1.  f_i is real-rooted, so |f_i| has one
  peak per gap (at its critical point) and is monotone outside the roots:
  M_i = max(|f_i(lo_i)|, |f_i(hi_i)|, |f_i(c)| for critical c in the box).

  Screen: |f_i(x)| <= M_i for every x in the box, so the endpoints and the
  two-pole start points give a lower bound R_i <= M_i at a few multiplies
  per point.  prod R_i >= 1 + screen_margin rejects a batch for certain
  (margin >> rounding of the N n products); only survivors get the full
  critical-point solve.  Stage 0 tries the endpoints alone (no sort, no
  divisions); stage 1 adds the start points.
*/

struct box_t { vec lo, hi; };

const double screen_margin = 1e-12;
const double borderline_tol = 1e-9;

double lane_product(const vec v) {
  double p = 1;
  for (int i = 0; i < N; i++) p *= v[i];
  return p; }

template <int n>
vec abs_eval_from_roots(const roots_t<n> &r, const vec x) {
  return abs(eval_from_roots<n>(r, x)); }

template <int n>
vec endpoint_max(const roots_t<n> &r, const box_t &box) {
  return max(abs_eval_from_roots<n>(r, box.lo), abs_eval_from_roots<n>(r, box.hi)); }

// lower bound R_i <= M_i per lane; r must be sorted
template <int n>
vec screen_lower_bound(const roots_t<n> &r, const box_t &box) {
  vec m = endpoint_max<n>(r, box);
  for (int k = 0; k < n-1; k++) {
    const vec x = two_pole_start<n>(r, k);
    const auto inside = (x > box.lo) & (x < box.hi);
    m = inside ? max(m, abs_eval_from_roots<n>(r, x)) : m; }
  return m; }

// M_i = max over the box of |f_i| per lane
template <int n>
vec max_abs_on_box(const roots_t<n> &r, const box_t &box) {
  vec m = endpoint_max<n>(r, box);
  for (const vec c: critical_points_from_roots<n>(r).points) {
    const auto inside = (c > box.lo) & (c < box.hi);
    m = inside ? max(m, abs_eval_from_roots<n>(r, c)) : m; }
  return m; }

// counter-based RNG: batch b is regenerated from (seed, b) alone
uint64_t splitmix64(uint64_t z) {
  z += 0x9e3779b97f4a7c15;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31); }

template <int n>
roots_t<n> batch_roots(const uint64_t seed, const uint64_t batch, const double rmax) {
  const uint64_t base = splitmix64(seed) + batch * (n * N);
  roots_t<n> r;
  for (int j = 0; j < n; j++)
    for (int i = 0; i < N; i++)
      r[j][i] = rmax * (2 * ((splitmix64(base + j * N + i) >> 11) * 0x1p-53) - 1);
  return r; }

struct search_stats { long batches = 0, endpoint_rejects = 0, survivors = 0, jackpots = 0, borderline = 0,
                      screen_misses = 0;
                      double worst_screen_ratio = 1; };

// check: also run the full solve on every batch, to confirm the screen
// never rejects a jackpot and to measure how tight R_i is
template <int n>
search_stats jackpot_search(const uint64_t seed, const long count, const double rmax,
                            const box_t &box, const bool check) {
  search_stats st;
  for (long b = 0; b < count; b++, st.batches++) {
    const roots_t<n> unsorted = batch_roots<n>(seed, b, rmax);
    if (!check && lane_product(endpoint_max<n>(unsorted, box)) >= 1 + screen_margin) {
      st.endpoint_rejects++;
      continue; }
    const roots_t<n> r = sorted<n>(unsorted);
    const double lower = lane_product(screen_lower_bound<n>(r, box));
    const bool rejected = lower >= 1 + screen_margin;

    if (check) {
      const double full = lane_product(max_abs_on_box<n>(r, box));
      if (rejected && full < 1) {
        st.screen_misses++;
        std::println("SCREEN MISS batch {} lower {} full {}", b, lower, full);
        std::fflush(stdout); }
      st.worst_screen_ratio = std::max(st.worst_screen_ratio, full / lower); }

    if (rejected) continue;
    st.survivors++;
    const vec M = max_abs_on_box<n>(r, box);
    const double norm_max = lane_product(M);
    if (std::fabs(norm_max - 1) < borderline_tol) {
      st.borderline++;
      std::println("BORDERLINE batch {} max|norm| = {:.17g} -- verify exactly", b, norm_max);
      std::fflush(stdout); }
    if (norm_max < 1) {
      st.jackpots++;
      std::println("JACKPOT batch {} max|norm| = {:.17g}  M = {}", b, norm_max, M);
      for (const vec rj: r) std::println("    roots {}", rj);
      std::fflush(stdout); } }
  return st; }


int main (int argc, char *argv[]) {

  while (++argv, --argc) putenv(*argv);

  long seed  = atol(getenv("seed")    ?: "0");
  bool debug = atoi(getenv("debug")   ?: "0");
  verbose    = atoi(getenv("verbose") ?: "0");

  if (seed == 0) {
    std::random_device rd;
    seed = rd(); }
  println("seed = {}", seed);

  if (const long batches = atol(getenv("batches") ?: "0"); batches > 0) {
    constexpr int n = 6;
    const double rmax = atof(getenv("rmax") ?: "1");
    const box_t box{ vec{} + atof(getenv("lo") ?: "-0.5"), vec{} + atof(getenv("hi") ?: "0.5") };
    const bool check = atoi(getenv("check") ?: "0");
    assert(all_lanes(box.lo < box.hi));
    println("jackpot search: {} batches, n = {}, roots in [-{}, {}], box [{}, {}]{}",
            batches, n, rmax, rmax, box.lo[0], box.hi[0], check ? ", check mode" : "");
    std::fflush(stdout);
    const auto t0 = std::chrono::steady_clock::now();
    const search_stats st = jackpot_search<n>(seed, batches, rmax, box, check);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    println("batches {}  endpoint rejects {}  survivors {}  jackpots {}  borderline {}  {:.1f} ns/batch",
            st.batches, st.endpoint_rejects, st.survivors, st.jackpots, st.borderline,
            1e9 * secs / st.batches);
    if (check)
      println("check: screen misses {}  worst M/R product ratio {:.17g}",
              st.screen_misses, st.worst_screen_ratio);
    return st.screen_misses == 0 ? 0 : 1; }

  polys x = X();
  if (debug) {
    println("x:");
    dump(x);

    polys w = (x*x) - vec{0,1,2,3};
    println("w:");
    dump(w);

    polys x23 = 2.0*x + 3;
    println("2*x+3:");
    dump(x23);

    polys f = { vec{ 1, 2, 3, 4},
                vec{ 5, -6, 7, 8},
                vec{ 2, 3, -2, 4},
                vec{ -5, 4, 3, 2} };
    println("f:");
    dump(f);

    polys g = derivative(f);
    println("g = derivative(f):");
    dump(g);

    polys gg = antiderivative(g);
    println("gg = antiderivative(g):");
    dump(gg);

    println("f+g:");
    dump(f+g);

    { polys z = { -vec{1,2,3,4}, vec{} + 1 };
      println("z:");
      dump(z); }

    { vec h = abs(f[0] * g[1]);
      println("h = {}", h); }

    { polys h2 = f * g;
      println("h2 = f * g:");
      dump(h2); }

    { polys s = x*x + 2*x - 3;
      println("s:");
      dump(s);

      vec ss = abs(eval(s, vec{0,1,2,3}));
      println("abs(s(0,1,2,3)): = {}", ss); }
  }

  { // now do an actual computation

    const int n = 6; // degree(f) = n-1
    vector<vec> r(n);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<> dist(-10.0, 10.0);
    for (auto &row: r)
      for (int i = 0; i < N; i++)
        row[i] = dist(rng);

    // we could sort these (using a circuit from sort.hs)

    polys f{ vec{}+1 };
    for (auto z: r)
      f = f * (x - z);

    println("=== finding min/max of f, using roots of f1 = f':");
    println("f:"); dump(f);
    println(">> using critical_points(f):");
    vector<vec> roots = critical_points(f);
    for (auto &v: roots)
      cout << v << "\n";

    /* Note: when we get a bad solution (in one or more lanes),
       we could just move to another starting position... */

    polys f1 = derivative(f);
    polys g1{ f1[n-1] };
    for (auto &z: roots)
      g1 = g1 * (x - z);
    println("f1:"); dump(f1);
    println("g1:"); dump(g1);
    polys h1 = f1 - g1;
    double err = 0;
    for (auto &v: h1)
      for (int i = 0; i < N; i++)
        err = max(err, abs(v[i]));
    println("L1-err = {}\n", err);

    println(">> using critical_points_from_roots(r):");
    roots_t<n> ra;
    std::ranges::copy(r, ra.begin());
    const auto [c, iterations] = critical_points_from_roots<n>(ra);
    roots_t<n-1> laguerre_sorted;
    std::ranges::copy(roots, laguerre_sorted.begin());
    laguerre_sorted = sorted<n-1>(laguerre_sorted);
    double diff = 0;
    for (int k = 0; k < n-1; k++) {
      println("{}   f(c) = {}", c[k], eval_from_roots<n>(ra, c[k]));
      diff = max(diff, max(abs(c[k] - laguerre_sorted[k]) / (1 + abs(c[k])))); }
    println("iterations = {}, max rel diff vs Laguerre = {}", iterations, diff);

  }

  return 0; }
