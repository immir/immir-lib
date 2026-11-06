#pragma once
// schurly shared core — single source of truth for the β-kernel infrastructure
// that is byte-identical across plethy / schurly / krony.  Header-only: each tool
// stays a single translation unit compiled with one command (paper-artifact
// property preserved).  No external dependencies — a local header does not
// violate the dependency-free property.
//
// Carve log:
//   - SHA-256 (struct Sha256): lifted 2026-06-22, verified byte-identical 3/3.
//   - mix64 / Key<W> β-machinery / List<W,C>: lifted 2026-06-22; Key block is
//     plethy/krony verbatim (schurly's key_hash differed only in brace style;
//     schurly gains the uninstantiated key_lowest_zero/key_between templates).
//   - Part + input parsing (parse_die/parse_symm/expand_symm/parse_latex/
//     parse_raw): lifted 2026-06-22, plethy/krony verbatim (schurly identical
//     code modulo 3 comment lines).  parse_arg dispatch stays per-tool.
//   - now_s / put_u32 / set_mem_limit_from_env / Map<W,C>: lifted 2026-06-22
//     (tier-2).  Map is plethy/krony verbatim (schurly differs by one comment);
//     it calls addc(), which is defined per-tool (the wide variants diverge —
//     krony's are overflow-checked) and forward-declared below.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <sys/resource.h>

using std::size_t;
using std::int64_t;
using std::uint8_t;
using std::uint32_t;
using std::uint64_t;

using Part = std::vector<int>;            // weakly decreasing positive parts

// ------------------------------------------------------------------- SHA-256

struct Sha256 {
  static constexpr uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t h[8];
  uint64_t len = 0;
  uint8_t buf[64];
  size_t fill = 0;

  Sha256() {
    static constexpr uint32_t H0[8] = {
      0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::memcpy(h, H0, sizeof h); }

  static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
      const uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1; }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      const uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
      const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = S0 + mj;
      hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2; }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh; }

  void update(const uint8_t* p, size_t n) {
    len += n;
    while (n) {
      const size_t take = std::min(n, (size_t)64 - fill);
      std::memcpy(buf + fill, p, take);
      fill += take; p += take; n -= take;
      if (fill == 64) { block(buf); fill = 0; } } }

  void final16(uint8_t out[16]) {
    const uint64_t bits = len * 8;
    const uint8_t one = 0x80;
    update(&one, 1);
    const uint8_t zero = 0;
    while (fill != 56) update(&zero, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; ++i) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    update(lenb, 8);
    for (int i = 0; i < 4; ++i) {
      out[4*i]   = (uint8_t)(h[i] >> 24);
      out[4*i+1] = (uint8_t)(h[i] >> 16);
      out[4*i+2] = (uint8_t)(h[i] >> 8);
      out[4*i+3] = (uint8_t)(h[i]); } }
};

constexpr uint32_t Sha256::K[64];

// ------------------------------------------------------- mix64 / β-key bitset
// 64-bit finalizer, and the β-number (Maya/abacus) bead representation: a
// partition as a bitset packed into W 64-bit words.

static inline uint64_t mix64(uint64_t x) {
  x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33; return x; }

template <int W>
struct Key {
  uint64_t w[W];
  bool operator==(const Key& o) const {
    bool eq = true;
    for (int i = 0; i < W; ++i) eq &= (w[i] == o.w[i]);
    return eq; }
};

template <int W>
static inline uint64_t key_hash(const Key<W>& k) {
  uint64_t h = 0x9e3779b97f4a7c15ULL;
  for (int i = 0; i < W; ++i) h = mix64(h ^ k.w[i]);
  return h;
}

template <int W> static inline bool key_bit(const Key<W>& k, int b) { return (k.w[b >> 6] >> (b & 63)) & 1; }
template <int W> static inline void key_set(Key<W>& k, int b)   { k.w[b >> 6] |=  (1ULL << (b & 63)); }
template <int W> static inline void key_clear(Key<W>& k, int b) { k.w[b >> 6] &= ~(1ULL << (b & 63)); }

// position of the lowest unset bit == len(partition) in the beta encoding
// (zero-parts occupy the contiguous low run 0..N-len-1; the first gap is len).
template <int W> static inline int key_lowest_zero(const Key<W>& k) {
  for (int i = 0; i < W; ++i)
    if (k.w[i] != ~0ULL) return i * 64 + __builtin_ctzll(~k.w[i]);
  return W * 64; }

// # set bits strictly between lo and hi (lo < hi)
template <int W>
static inline int key_between(const Key<W>& k, int lo, int hi) {
  const int a = lo + 1, b = hi - 1;
  if (a > b) return 0;
  const int wa = a >> 6, wb = b >> 6;
  const uint64_t ma = ~0ULL << (a & 63);
  const uint64_t mb = ((b & 63) == 63) ? ~0ULL : ((1ULL << ((b & 63) + 1)) - 1);
  if (wa == wb) return __builtin_popcountll(k.w[wa] & ma & mb);
  int c = __builtin_popcountll(k.w[wa] & ma) + __builtin_popcountll(k.w[wb] & mb);
  for (int i = wa + 1; i < wb; ++i) c += __builtin_popcountll(k.w[i]);
  return c; }

// ------------------------------------------------------------------ term list

template <int W, class C>
struct List {
  std::vector<Key<W>> keys;
  std::vector<C> coefs;
  size_t size() const { return keys.size(); }
};

// ----------------------------------------------------------------- parsing
// Shared partition input parsing.  parse_arg (auto-detect dispatch) stays
// per-tool; parse_die throws (callers in main translate to a clean error).

[[noreturn]] static void parse_die(const std::string& msg, const std::string& in) {
  throw std::runtime_error(msg + " in '" + in + "'"); }

static Part parse_symm(const std::string& s) {
  Part v;
  for (size_t i = 0; i < s.size(); ) {
    const char c = s[i];
    if (c >= '1' && c <= '9') { v.push_back(c - '0'); ++i; }
    else if (c >= 'A' && c <= 'F') { v.push_back(c - 'A' + 10); ++i; }
    else if (c == '|') {
      const size_t st = ++i;
      int n = 0;
      while (i < s.size() && isdigit((unsigned char)s[i])) n = n * 10 + (s[i++] - '0');
      if (i == st || n <= 0) parse_die("bad '|NN' escape", s);
      v.push_back(n); }
    else parse_die(std::string("unrecognised symmetrica character '") + c + "'", s); }
  std::sort(v.begin(), v.end(), std::greater<int>());
  return v; }

// Expand Symmetrica "letter^N" repetition (letter = digit, hex, or |NN).
static std::string expand_symm(const std::string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ) {
    std::string letter;
    if (in[i] == '|') { letter += in[i++]; while (i < in.size() && isdigit((unsigned char)in[i])) letter += in[i++]; }
    else letter += in[i++];
    int rep = 1;
    if (i < in.size() && in[i] == '^') {
      const size_t st = ++i;
      rep = 0;
      while (i < in.size() && isdigit((unsigned char)in[i])) rep = rep * 10 + (in[i++] - '0');
      if (i == st) parse_die("'^' must be followed by an integer", in); }
    for (int k = 0; k < rep; ++k) out += letter; }
  return out; }

static Part parse_latex(const std::string& s) {
  size_t i = 0;
  if (i < s.size() && s[i] == 's') ++i;
  if (i < s.size() && s[i] == '_') ++i;
  std::string body;
  if (i < s.size() && s[i] == '{') {
    int depth = 1;
    const size_t start = ++i;
    while (i < s.size() && depth > 0) {
      if (s[i] == '{') ++depth;
      else if (s[i] == '}' && --depth == 0) break;
      ++i; }
    if (depth != 0) parse_die("unclosed brace", s);
    body = s.substr(start, i - start); ++i;
    if (i != s.size()) parse_die("trailing input after closing brace", s); }
  else body = s.substr(i);

  const auto trim = [](std::string& t) {
    while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
    size_t k = 0;
    while (k < t.size() && isspace((unsigned char)t[k])) ++k;
    t.erase(0, k); };
  trim(body);
  if (body.size() >= 2 && body.front() == '(' && body.back() == ')') { body = body.substr(1, body.size() - 2); trim(body); }

  Part parts;
  for (size_t pos = 0; pos < body.size(); ) {
    while (pos < body.size() && isspace((unsigned char)body[pos])) ++pos;
    if (pos >= body.size()) break;
    if (!isdigit((unsigned char)body[pos])) parse_die(std::string("unexpected '") + body[pos] + "'", s);
    int val = 0;
    while (pos < body.size() && isdigit((unsigned char)body[pos])) val = val * 10 + (body[pos++] - '0');
    int rep = 1;
    while (pos < body.size() && isspace((unsigned char)body[pos])) ++pos;
    if (pos < body.size() && body[pos] == '^') {
      ++pos;
      while (pos < body.size() && isspace((unsigned char)body[pos])) ++pos;
      const bool braced = pos < body.size() && body[pos] == '{';
      if (braced) { ++pos; while (pos < body.size() && isspace((unsigned char)body[pos])) ++pos; }
      if (pos >= body.size() || !isdigit((unsigned char)body[pos])) parse_die("'^' must be followed by an integer", s);
      rep = 0;
      while (pos < body.size() && isdigit((unsigned char)body[pos])) rep = rep * 10 + (body[pos++] - '0');
      if (braced) {
        while (pos < body.size() && isspace((unsigned char)body[pos])) ++pos;
        if (pos >= body.size() || body[pos] != '}') parse_die("unclosed '^{...}'", s);
        ++pos; } }
    if (val > 0) for (int k = 0; k < rep; ++k) parts.push_back(val);
    while (pos < body.size() && isspace((unsigned char)body[pos])) ++pos;
    if (pos < body.size()) {
      if (body[pos] != ',') parse_die(std::string("expected ',' between parts, got '") + body[pos] + "'", s);
      ++pos; } }
  std::sort(parts.begin(), parts.end(), std::greater<int>());
  return parts; }

static Part parse_raw(const std::string& a) {
  Part p;
  if (a.empty()) return p;
  const char* const end = a.data() + a.size();
  for (const char* q = a.data(); q <= end; ) {
    const char* comma = std::find(q, end, ',');
    if (comma == q) parse_die("empty part", a);
    int v;
    const auto [ptr, ec] = std::from_chars(q, comma, v);
    if (ec == std::errc::result_out_of_range) parse_die("part too large", a);
    if (ec != std::errc{} || ptr != comma) parse_die("non-integer part", a);
    if (v <= 0) parse_die("non-positive part", a);
    if (!p.empty() && v > p.back()) parse_die("ascending part (parts must be non-increasing)", a);
    p.push_back(v);
    if (comma == end) break;
    q = comma + 1; }
  return p; }

// ----------------------------------------------------------- misc helpers

static double now_s() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9; }

static inline char* put_u32(char* p, uint32_t v) {
  char tmp[12]; int k = 0;
  do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (k) *p++ = tmp[--k];
  return p;
}

// Optional address-space cap: <VAR>=N (gigabytes) makes a runaway run hit the
// new-handler (set per-tool, with the tool's own message) instead of OOM-killing
// the host.  No-op if the env var is unset or non-positive.
static inline void set_mem_limit_from_env(const char* var) {
  if (const char* e = std::getenv(var)) {
    const long gb = std::atol(e);
    if (gb > 0) {
      const rlim_t bytes = (rlim_t)gb * 1024 * 1024 * 1024;
      struct rlimit rl{bytes, bytes};
      setrlimit(RLIMIT_AS, &rl); } } }

// ------------------------------------------------------------- hash map
// Open-addressing coefficient map keyed by β-key.  add() uses addc(), whose
// overloads are defined PER-TOOL (the wide/i128 variants diverge — krony's are
// overflow-checked).  Forward-declare the fundamental-type overloads so the
// template body resolves them; tool-specific class-typed coefficients (e.g.
// krony's iLimb) are found by ADL at instantiation.

static inline int64_t addc(int64_t, int64_t);
static inline __int128 addc(__int128, __int128);

template <int W, class C>
struct Map {
  struct Entry { Key<W> k; C v; };
  std::vector<Entry> e;
  size_t mask = 0, cnt = 0;

  static Key<W> empty_key() {
    Key<W> k;
    for (int i = 0; i < W; ++i) k.w[i] = ~0ULL;
    return k; }

  void init(size_t cap_pow2) {
    size_t cap = 1;
    while (cap < cap_pow2) cap <<= 1;
    e.assign(cap, Entry{empty_key(), C(0)});
    mask = cap - 1; cnt = 0; }
  Map() { init(1 << 6); }

  void grow() {
    std::vector<Entry> old = std::move(e);
    e.assign(old.size() * 2, Entry{empty_key(), C(0)});
    mask = e.size() - 1;
    const Key<W> EMP = empty_key();
    for (const Entry& en : old) {
      if (en.k == EMP) continue;
      size_t i = key_hash(en.k) & mask;
      while (!(e[i].k == EMP)) i = (i + 1) & mask;
      e[i] = en; } }

  inline void add(const Key<W>& k, C v) {
    if ((cnt + 1) * 8 > e.size() * 5) grow();
    const Key<W> EMP = empty_key();
    size_t i = key_hash(k) & mask;
    for (;;) {
      if (e[i].k == EMP) { e[i].k = k; e[i].v = v; ++cnt; return; }
      if (e[i].k == k)   { e[i].v = addc(e[i].v, v); return; }
      i = (i + 1) & mask; } }

  // batch insert with software prefetch (hides DRAM latency on big maps)
  void add_batch(const Entry* es, size_t n) {
    constexpr size_t PF = 16;
    for (size_t j = 0; j < n; ++j) {
      if (j + PF < n)
        __builtin_prefetch(&e[key_hash(es[j + PF].k) & mask], 1, 1);
      add(es[j].k, es[j].v); } }

};
