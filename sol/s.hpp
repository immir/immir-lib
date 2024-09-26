/*

  Classes and functions for solving Solitaire / Klondike games.
  Immir, Circa 2023

  Nomenclature:

  Foundation - final position of cards
  Tableau - piles of size 1 .. 7
  Stock - unrevealed deck
  Talon - upturned stock cards, 3 at a time

  Notes:

  As we are searching, not playing, we don't care which cards are
  turned up except in so far as it affects possible actions.

  We currently do not really make use of knowledge of unrevealed cards,
  except to estimate minimum moves required and trim the search tree.

  Note: if we make suits contiguous, then we could maybe use mmx/sse
  instructions to check for non-zero 16-bit words for each suit...

  TODO: consider using bit compress to collapse a suit down to a
  contiguous range.

*/

#include <strings.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <atomic>
#include <ranges>
#include <random>


#include <stdint.h>
#include <x86intrin.h>

using std::cout, std::reverse, std::string, std::ostream;
using std::endl, std::vector, std::unordered_map, std::setw;
using std::stringstream, std::to_string, std::atomic;
using std::views::stride, std::views::drop, std::left;
using std::views::enumerate, std::flush;

typedef int64_t  i64;
typedef uint64_t u64;
typedef unsigned __int128 u128;

#define unless(x) if(!(x)) // thanks Larry...

auto min(auto x, auto y) { return x < y ? x : y; }
auto max(auto x, auto y) { return x > y ? x : y; }

#define popcnt  __builtin_popcountl
#define leadz   __builtin_clzll
#define trailz  __builtin_ctzll
#define rtc     __rdtsc


// ----------------------------------------------------------------------
// Mixing function for hasing; this is based on SPECK
// TODO: remove macros? use lambdas?

#if defined(__AES__)

// this does seem faster, and flat enough (checked with ./s bound=200)
#include <wmmintrin.h>
static u64 mix(u64 x, u64 y) {
  __m128i xy = _mm_set_epi64x(x,y);
  __m128i yx = _mm_set_epi64x(y,x);
  __m128i mm = _mm_aesenc_si128(xy,yx);
  mm = _mm_aesenc_si128(mm,xy);
  mm = _mm_aesenc_si128(mm,yx);
  return _mm_cvtsi128_si64x(mm); }

#else

// mixing function based on SPECK round function
#define ROR _lrotr
#define ROL _lrotl
#define R(x, y, k) (x = ROR(x, 8), x += y, x ^= k, y = ROL(y, 3), y ^= x)
#define ROUNDS 10

static u64 mix(u64 x, u64 y) {
  u64 a = 0x5555555555555555UL, b = 0xaaaaaaaaaaaaaaaaUL;
  for (u64 i = 0; i < ROUNDS; i++) R(a, b, i), R(x, y, b);
  return x ^ y; }

#endif

// ----------------------------------------------------------------------

#include <time.h>
__attribute__((unused))
static double wall () {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return t.tv_sec + t.tv_nsec/1e9; }

// ----------------------------------------------------------------------

typedef enum { S, D, C, H } suit_t;
typedef enum { A=1, T=10, J=11, Q=12, K=13 } rank_t;

const char suit_str[] = "SDCH";
const char rank_str[] = "#A23456789TJQK";

const string suit_names[] = {"S", "D", "C", "H"};
const string rank_names[] = {"#", "A", "2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K" };

// ----------------------------------------------------------------------
/*
  Notes:

  (*) Each card corresponds to a bit position in a 64-bit u64.

  (*) A run of cards (either in the Foundation or in the Tableau) can
  be represented as a union of cards with the right bit ordering:

     0000000000111111111122222222223333333333444444444455555555556666
     0123456789012345678901234567890123456789012345678901234567890123

         AAAA22223333444455556666777788889999TTTTJJJJQQQQKKKK
         SDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCH

   So, if x is a u64 card, x>>2 is its rank, x&3 its suit. Here we've
   alternated red/black (for any reason? if not, consider changing?).

   (*) Zero represents an empty slot.

*/

// methods for singleton runs; i.e. cards!

static int suit(u64 c)  { int i = trailz(c); return i & 3; }
static int rank(u64 c)  { int i = trailz(c); return i >> 2; }
static int color(u64 c) { int i = trailz(c); return i & 1; }

static const u64 suits[4] = {
  [S] = 0x1111111111111UL << 4,
  [D] = 0x1111111111111UL << 5,
  [C] = 0x1111111111111UL << 6,
  [H] = 0x1111111111111UL << 7 };

static u64 card(const string &str) {
  int r = index(rank_str, str[0]) - rank_str;
  int s = index(suit_str, str[1]) - suit_str;
  return 1UL<<(4*r + s); }

// static u64 card(u64 x)  { assert(popcnt(x)==1); return x; }
// static u64 card(int rank, int suit) { return card(1UL<<(4*rank + suit)); }

// TODO: rename to bot and top???
static u64 min(u64 x) { return x & ~(x-1); }

__attribute__((unused))
static u64 max(u64 x) { if (!x) return 0; int i = 63-leadz(x); return 1UL<<i; }

static string str(const u64 &r) {
  stringstream ss;
  u64 x = r;
  if (x == 0) {
    ss << "--";
    return ss.str(); }
  while (x != 0) {
    int i = 63 - leadz(x);
    u64 c = 1UL << i;
    x ^= c;
    ss << rank_names[rank(c)] << suit_names[suit(c)];
    if (popcnt(x) > 0) ss << " "; }
  return ss.str(); }

/*
typedef enum {
  Draw,
  T2T,
  T2F,
  S2T,
  S2F,
  F2T,
} MoveType;
*/

typedef struct Move_s {
  /* Simple class for tracking and reporting on moves for a solution */
  u64 c{0}, d{0};
  static struct Move_s draw()             { return {0,0}; }
  static struct Move_s to_found(u64 c)    { return {c,0}; }
  static struct Move_s move(u64 c, u64 d) { return {c,d}; }
  string to_string() const {
    return (c == 0 ? "draw" :
            d == 0 ? str(c)+" to F" :
            d == -1UL ? str(c) +" to --" :
            str(c)+" to "+str(d)); }
} Move;


typedef struct GameState_s {

  u64 found[4]{0};
  u64 tableau[28]{0};
  u64 stock[25]{0};  // stock and talon (extra slot for empty cell at pos 0)
  i64 stock_size{0};
  i64 pos{0};        // talon pos in stock[24+1] - start at empty slot
  i64 moves{0};
  i64 moves_min{0};

  // precomputed data for recursion computations:
  u64 bigger[28]{0}; // for each (down) tableau card, mask of bigger cards - or scan?!

  GameState_s() { /* empty state */ }

  // generate a random game using a seed
  GameState_s(int seed) {
    vector<u64> cards;
    for (int i = 0; i < 52; i++)
      cards.push_back(1UL<<(i+4));
    std::default_random_engine rng(seed);
    std::shuffle(cards.begin(), cards.end(), rng);
    // now deal them out
    int k = 0;
    for (int i = 0; i <= 6; i++)
      for (int j = 0; j <= i; j++)
        tab(i)[j] = cards[k++];
    for (int i = 0; i < 24; i++)
      stock[i+1] = cards[k++], stock_size++;
    assert(k == 52); }

  // read a file for initial game state
  GameState_s(string filename, bool rev = false) {
    string data;
    { std::ifstream t(filename);
      std::stringstream buffer;
      buffer << t.rdbuf();
      data = buffer.str(); }
    stringstream ss(data);
    string s;
    u64 seen = 0UL;
    for (int i = 0; i <= 6; i++)
      for (int j = 0; j <= i; j++) {
        ss >> s;
        if (s == "rev") { cout << "REV" << endl; rev = true; j--/*HACK*/; continue; }
        if (seen & card(s)) {
          cout << "card " << s << " seen before!" << endl;
          exit(1); }
        seen |= card(s);
        tab(i)[rev ? j : i-j] = card(s); }
    for (int i = 0; i < 3*8; i++) {
      ss >> s;
      if (seen & card(s)) {
        cout << "card " << s << " seen before!" << endl;
        exit(1); }
      seen |= card(s);
      stock[rev ? i+1 : 24-i] = card(s);
      ++stock_size; } }

  // this returns a pointer to the start of the given Tableau in s
  u64* tab(int i) const {
    return (u64*) tableau + i*(i+1)/2; }

  // this returns a pointer to the start of the given Bigger in s
  u64* big(int i) const {
    return (u64*) bigger + i*(i+1)/2; }

  // precompute data for recursion
  // NB: no need to optimize
  void precompute () {
    for (int i = 0; i <= 6; i++) {
      big(i)[0] = 0;
      for (int j = 1; j <= i; j++) { // down cards
        assert(popcnt(tab(i)[j]) == 1); // let's be careful out there...
        big(i)[j] = (~(tab(i)[j]-1)) & suits[suit(tab(i)[j])]; // same-suit cards bigger than tab(i)[j]
        big(i)[0] |= big(i)[j]; } }
  }

  void compute_moves_bound() {

    int f = (popcnt(found[0]) + popcnt(found[1]) +
             popcnt(found[2]) + popcnt(found[3]));

    moves_min = moves;

    // we at least need a move for each card not in the foundation:
    moves_min += 52 - f;

    /* stock: we need to at least turn over the rest of the cards; it's
       possible to then use them all at that point... */
    moves_min += (stock_size - pos + 2) / 3;

    for (int i = 1; i <= 6; i++) { // first tableau pile has no down cards

      // TODO: combine both of these stanzas - then remove "precomputed"
      // and updates of the scan? Reverse order of stanzas, then output
      // of second is essentially what big(i)[0] is, right?

      // first, any up-cards bigger than covered down ones?
      u64 up_cards         = tab(i)[0];
      u64 bigger_than_down = big(i)[0];
      if (bigger_than_down & up_cards)
        ++moves_min;

      // now, consider bigger down cards over smaller ones
      u64 bigger_than_down_scan = big(i)[i];
      for (int j = i-1; j >= 1; j--) { // bottom up
        if (tab(i)[j] & bigger_than_down_scan)
          ++moves_min;
        bigger_than_down_scan |= big(i)[j]; }
    } }

  // draw cards from stock (if any)
  void draw() {
    assert(stock_size > 0);
    if (pos == stock_size) // automatically refresh if needed
      pos = 0;             // which is a free move; we don't even record it
    pos = min(pos + 3, stock_size);
    assert(pos > 0); }

  u64 take_from_tab(int i, u64 c) {
    assert(popcnt(c) == 1);
    u64 cc = tab(i)[0] & ((c<<1)-1); // take c and lower cards
    tab(i)[0] &= ~cc;
    if (tab(i)[0] == 0) {
      for (int j = 1; j <= i; j++)
        tab(i)[j-1] = tab(i)[j], big(i)[j-1] = big(i)[j];
      tab(i)[i] = 0, big(i)[i] = 0;
      big(i)[0] = 0; // update union of bigger cards
      for (int j = 1; j <= i; j++)
        big(i)[0] |= big(i)[j]; }
    return cc; }

  void add_to_found(u64 c) {
    assert(popcnt(c) == 1);
    int i = suit(c);
    found[i] |= c; }

  void add_to_tab(int i, u64 c) {
    assert(c != 0);
    tab(i)[0] |= c; }

  u64 take_from_stock() {
    assert(pos > 0);
    assert(pos <= stock_size);
    u64 c = stock[pos];
    assert(popcnt(c) == 1);
    /* TODO: keep two positions; pos points to top of drawn cards;
       rest points to first unused card;

       -- XX XX XX -- -- -- YY YY YY YY YY YY
       0  1  2  3  .  .  .  6  7  8  9  10 11

       pos = 3, rest = 6
       draw 3: copy 3 down to pos;
       use a card: just remove it and --pos

       TODO: is this cheaper overall? likely not enough, right?
    */
    for (int j = pos; j <= stock_size-1; j++)
      stock[j] = stock[j+1];
    stock[stock_size] = 0UL;
    --stock_size;
    --pos;
    assert(pos >= 0);
    return c; }

  /* To recover history of successful game, we follow parent (prev)
     pointers back along path taken to get there and use stored
     information regarding the move taken to get to each state;
     NB: Refreshing stock is now implicit (as it doesn't cost a move). */

  struct GameState_s *prev{NULL};
  Move m;

  struct GameState_s move(Move mm) {
    // do a move and return new state with record of last (given) move
    struct GameState_s ss = *this;
    ss.prev = this;
    ss.m = mm;
    ++ss.moves;
    return ss; }

  vector<string> history() const {
    vector<string> h;
    for (auto s = this; s && s->prev; s = s->prev)
      h.push_back(s->m.to_string());
    reverse(h.begin(), h.end());
    return h; }

} GameState;

static u64 hash(const GameState &s) {
  // Note: adjusted our hash so that order of piles doesn't matter(?)
  u64 h = 0;
  for (int i = 0; i < 4;  i++)
    h ^= mix(i, s.found[i]); // foundation is same order by choice
  for (int i = 0; i <= 6; i++)
    for (int j = 0; j <= i && s.tab(i)[j]; j++)
      h ^= mix(j+100, s.tab(i)[j]);
  h ^= mix(200, s.pos); // just stock pos seems to be sufficient
  return h; }

static string format_history(vector<string> hist) {
  // TODO: refactor using range::views etc
  stringstream os;
  for (auto [i,h]: enumerate(hist)) // NB: we get a ref for h!
    h = to_string(i+1) + " - " + h; // so we can mutate it!
  // TODO: really just want to chunk (rows) then transpose...
  int cols = 5, rows = (hist.size() + cols-1) / cols, wid  = 70 / cols;
  for (int i = 0; i < rows; i++) {
    os << "|";
    for (auto h: hist | drop(i) | stride(rows))
      os << " " << setw(wid) << left << h << setw(1) << " |";
    os << endl; }
  return os.str(); }

ostream& operator<<(ostream& os, const GameState& s) {
  os << "---GameState-----------------" << endl;
  for (int i = 0; i <= 3; i++) {
    os << "Foundation[" << (i+1) << "] = ";
    os << str(s.found[i]) << endl; }
  for (int i = 0; i <= 6; i++) {
    os << "Tableau[" << (i+1) << "] = ";
    for (int j = 0; j < i+1; j++) {
      auto r = s.tab(i)[j];
      os << str(r) << " "; }
    os << endl; }
  os << "Stock = ";
  for (int i = 0; i < 25; i++)
    os << str(s.stock[i]) << " ";
  os << endl;
  os << "Talon pos = " << s.pos << endl;
  os << "stock size = " << s.stock_size << endl;
  os << "moves = " << s.moves << endl;
  if (s.prev)
    os << format_history(s.history());
  os << "-----------------------------" << endl;
  return os; }

static u64 f_next(u64 x) {
  // next card for foundation (i.e. same suit, 1 higher rank)
  u64 any_ace = 0xf<<4;
  int i = 63 - leadz(x|1); // leadz undefined for x==0
  return any_ace | (1UL<<(i+4)); }

static u64 t_next(u64 x) {
  /* for top card x in tableau, what cards can move onto it?
     For example, for each rank 4 cards marked with '^':
     AAAA22223333444455556666777788889999TTTTJJJJQQQQKKKK
     SDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCHSDCH
     .        1 1^      1 & 3    suit 0 color 0   1=1+suit+color
     .       1 1  ^     3 & 5    suit 1 color 1   3
     .        1 1  ^    3 & 5    suit 2 color 0   3
     .       1 1    ^   5 & 7    suit 3 color 1   5 */
  x = min(x);
  u64 any_king = 0xfUL << (4*K);
  if (x == 0) return any_king;
  int i = 1 + suit(x) + color(x);
  return (x >> i) | (x >> (i+2)); }

