#if 0
set -x
g++ -O2 -g -std=gnu++23 -march=native -Wall -Wextra -Werror -o ${0%.*} $0 && ./${0%.*} $*
exit
#endif

#include <cmath>
#include <print>
#include <format>
#include <iostream>
#include <vector>
#include <cassert>
#include <random>

using std::vector, std::cout, std::ostream, std::string, std::print, std::println;
using std::min, std::max;

#define append push_back

#define N 4

using vec = double __attribute__((__vector_size__(N*sizeof(double))));
using polys = vector<vec>;
const double ε = 1e-15;


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
    print("lane {}: ", i);
    for (int j = d-1; j >= 0; j--) {
      double c = f[j][i];
      if (c == 0) continue;
      bool neg = c < 0;
      if (neg) print(" - ");
      else if (!first) print(" + ");
      if (fabs(c) != 1 || j == 0) print("{}", fabs(c));
      if (j == 1) print(" x");
      else if (j > 1) print(" x^{}", j);
      first = false; }
    println(); } }

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
polys operator- (auto a, const polys &f) { polys g = f; g[0] -= a; return g; }

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

vector<vec> critical_points(polys f, vec z0) {
  // find critical points of polys in f[] using Newton iterations with
  // starting values in z0[]
  vector<vec> roots; // return value
  polys f1 = derivative(f);
  polys f2 = derivative(f1);
  polys x = { vec{}, vec{}+1 };
  while (degree(f1) > 0) {

    vec z = z0;
    for (int iter = 0; iter < 12; iter++)
      z = z - eval(f1,z) / eval(f2,z);

    // TODO: check convergence

    // z is a collection of roots of f'
    roots.append(z);

    // remove these roots for next iteration
    f1 = f1 / (x - z);
    f2 = derivative(f1); }

  return roots; }


int main (int argc, char *argv[]) {

  (void) argv[argc-1];

  polys x = { vec{}, vec{}+1 };
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

  { const int n = 5;
    vector<vec> r(n);

    thread_local std::random_device rd;
    thread_local std::mt19937 rng(rd());
    std::uniform_real_distribution<> dist(-10.0, 10.0);
    for (auto &row: r)
      for (int i = 0; i < N; i++)
        row[i] = dist(rng);

    // sort the roots in each column
    // do I care? or just min/max once for Newton's method each time
    // (that's probably sufficient)
    auto op = [&r] (int i, int j) {
      vec a = min(r[i], r[j]), b = max(r[i],r[j]);
      r[i] = a, r[j] = b; };
    // sort.hs: sort 5 -- a non-branching sorting circuit for 5 items
    assert(n == 5);
    op(0,1); op(3,4); op(2,4); op(2,3); op(0,3); op(0,2); op(1,4); op(1,3); op(1,2);

    // TODO: I may not need this

    println("sorted roots:");

    polys f{ vec{}+1 }; // 1
    for (auto z: r)
      f = f * (x - z);

    println("=== finding min/max of f, using roots of f1 = f':");
    println("f:"); dump(f);
    println(">> using critical_points(f,r[0]):");
    vector<vec> roots = critical_points(f, r[0]);
    for (auto &v: roots)
      cout << v << "\n";
    // working!

  }

  return 0; }
