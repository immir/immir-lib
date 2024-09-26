
#include "s.hpp"

// TODO: use unicode suits? larger font?
// TODO: use aes intrinsic for hash?

/* TODO: many of these globals were created to try and get
   depth-first parallelism working... which seems to have failed.
   REFACTOR? */

static long count;
static long depth;
static long bound;
static long best = 0;
static bool verbose = 0;
static bool opt_W, opt_F;
static vector<string> answer;

void recurse (GameState d);

int main (int argc, char *argv[]) {

  // immir's command-line/env args
  while (++argv, --argc && strchr(*argv, '='))
    putenv(*argv);

  bool rev = atoi(getenv("rev")     ?: "0");
  int seed = atoi(getenv("seed")    ?: "0");

  bound = atoi(getenv("B")  ?: "100");
  opt_W = atoi(getenv("W")  ?: "0");
  opt_F = atoi(getenv("F")  ?: "0");

  int bound0 = bound;

  cout << "(B)ound             = " << bound << endl;
  cout << "(W)hole piles move  = " << opt_W << endl;
  cout << "(F)oundation redraw = " << opt_F << endl;

  GameState s;

  if (seed) {
    cout << "seed  = " << seed << endl;
    GameState ss(seed);
    s = ss;
  } else {
    string filename = argc > 0 ? *argv : "cate.txt";
    cout << "reading file: " << filename << endl;
    GameState ss(filename, rev);
    s = ss; }

  cout << "=== starting state ===" << endl;
  cout << s;

#if 0
  { double tm0 = wall();
    cout << "=== calling recurse ===" << endl;
    recurse(s);
    cout << endl;
    printf ("time: %.2lf seconds.\n", wall()-tm0);
    printf ("count = 2^%.4f\n", log2((double)count));
    printf ("max depth = %ld\n", depth);
    printf ("best = %ld\n", best);
    if (answer.size() > 0 && !verbose)
      cout << format_history(answer);
    printf ("!! seed=%d B=%d W=%d F=%d t=%.2lf logc=%.4f maxd=%ld n=%ld\n",
            seed, bound0, opt_W, opt_F, wall()-tm0, log2((double)count),
            depth, best); }
#else
  { double tm_all = wall();
    for (int F = 0; F <= 1; F++)
      for (int W = 1; W >= 0; W--)
        { double tm0 = wall();
          int B = bound;
          opt_F = F, opt_W = W;
          printf("=== calling recurse W=%d F=%d B=%ld ===\n", opt_W, opt_F, bound);
          recurse(s);
          cout << endl;
          printf ("time: %.2lf seconds.\n", wall()-tm0);
          printf ("count = 2^%.4f\n", log2((double)count));
          printf ("max depth = %ld\n", depth);
          printf ("best = %ld\n", best);
          if (answer.size() > 0 && !verbose)
            cout << format_history(answer);
          printf ("!! seed=%d B=%d W=%d F=%d t=%.2lf logc=%.4f maxd=%ld n=%ld\n",
                  seed, B, opt_W, opt_F, wall()-tm0, log2((double)count),
                  depth, best);
          answer.clear();
          bound = best - 1; }
    printf("!!! seed=%d B=%d t=%.2lf n=%ld\n", seed, bound0, wall()-tm_all, best); }
#endif

  return 0; }

// ----------------------------------------------------------------------

void recurse (GameState s, unordered_map<int,int> &seen) {

  if (verbose) do { // hack: report performance
    static long done = 0;
    static double tm0 = wall();
    static long mask = 0xffff;
    ++done;
    if (done & mask) break;
    printf("2^%.2f done per second\n", log2(done/(wall() - tm0)));
    mask |= mask<<1;
  } while (0);

  // bail if we've seen this state with no better moves
  { auto h = hash(s);
    auto m = seen.find(h);
    if (m != seen.end() && s.moves >= m->second)
      return;
    seen[h] = s.moves; }

  ++count; // to report on how many states we actually consider
  if (s.moves > depth) depth = s.moves; // for reporting

  // how many cards have made it to the Foundation?
  // NB: popcnt is cheap enough; no need for extra (redundant) data
  int found = (popcnt(s.found[0]) + popcnt(s.found[1]) +
               popcnt(s.found[2]) + popcnt(s.found[3]));

  if (found == 52 && s.moves <= bound) {
    if (verbose)
      cout << "solution!" << endl << s;
    else {
      cout << s.moves << " " << flush;
      answer = s.history(); }
    best = s.moves;
    bound = s.moves - 1;
    return; }

  int moves_min = s.moves_min;
  assert(moves_min);

  // abort if we know we'll exceed the bound before success:
  if (moves_min > bound) return;

  // --- now recurse on each possible move from here

  /* We always turn over new stock cards if there are none left!
     NB: it *must* be done at some point (and gives a human player
     more information, so should always be preferred during play) */
  if (s.pos == 0 && s.stock_size > 0) { // auto-draw
    GameState ss = s.move(Move::draw());
    ss.draw();
    // NB: the return here means we skip the other options below
    ss.compute_moves_bound();
    return recurse(ss, seen); }

  // find cards that can go to Foundation
  {
    u64 fmask = 0; // foundation mask
    for (int i = 0; i <= 3; i++)
      fmask |= f_next(s.found[i]);

    for (int i = 0; i <= 6; i++) {
      u64 c = min(s.tab(i)[0]);
      unless (c & fmask) continue; // empty
      assert(popcnt(c) == 1);

      { GameState ss = s.move(Move::to_found(c));
        u64 cc = ss.take_from_tab(i, c);
        assert(c == cc); (void) cc;
        ss.add_to_found(c);
        ss.compute_moves_bound();
        recurse(ss, seen);
        if (moves_min > bound) return;
      } }

    do {
      if (s.pos == 0) break;
      assert(s.stock_size >= 1);
      u64 c = s.stock[s.pos];
      assert(popcnt(c) == 1);
      unless (c & fmask) break;

      GameState ss = s.move(Move::to_found(c));
      u64 cc = ss.take_from_stock();
      assert(c == cc); (void) cc;
      ss.add_to_found(c);
      ss.compute_moves_bound();
      recurse(ss, seen);
      if (moves_min > bound) return;
    } while (0);
  }

  u64 tmask = 0; // tableau mask - out here to be reused
  for (int i = 0; i <= 6; i++)
    tmask |= t_next(s.tab(i)[0]);

  // from stock to tab
  do {
    if (s.pos == 0) break;
    u64 c = s.stock[s.pos];
    assert(popcnt(c) == 1);
    unless (c & tmask) break;
    for (int i = 0; i <= 6; i++) {
      unless (c & t_next(s.tab(i)[0])) continue;
      u64 c2 = rank(c) == K ? -1 : min(s.tab(i)[0]);
      GameState ss = s.move(Move::move(c, c2));
      u64 cc = ss.take_from_stock();
      assert(cc == c);
      ss.add_to_tab(i, cc);
      ss.compute_moves_bound();
      recurse(ss, seen);
      if (moves_min > bound) return; }
  } while (0);

  { // from tab to tab
    for (int i = 0; i <= 6; i++) {
      u64 mm = s.tab(i)[0];
      if (opt_W) mm = max(mm); // (W)hole piles must move
      mm = mm & tmask;
      while (mm) { // for each choice of card that can move
        u64 c = mm & ~(mm - 1);
        mm ^= c;
        for (int j = 0; j <= 6; j++) {
          unless (c & t_next(s.tab(j)[0])) continue;
          if (rank(c) == K && (i == 0 || s.tab(i)[1] == 0)) continue;
          u64 c2 = rank(c) == K ? -1 : min(s.tab(j)[0]);
          GameState ss = s.move(Move::move(c,c2));
          u64 cc = ss.take_from_tab(i, c);
          assert(max(cc) == c);
          ss.add_to_tab(j, cc);
          ss.compute_moves_bound();
          recurse(ss, seen);
          if (moves_min > bound) return; } } } }

  // draw new cards?
  if (s.stock_size > 0) {
    assert(s.pos <= s.stock_size); // because of auto-draw earlier
    GameState ss = s.move(Move::draw());
    ss.draw();
    ss.compute_moves_bound();
    recurse(ss, seen);
    if (moves_min > bound) return; }

  if (opt_F) // allow moves from (F)oundation
    for (int i = 0; i < 4; i++) {
      u64 c = max(s.found[i]);
      unless (c & tmask) continue; // this one cannot move back to tableau
      // find which pile
      for (int j = 0; j <= 6; j++) {
        unless (c & t_next(s.tab(j)[0])) continue;
        u64 cc = min(s.tab(j)[0]);
        GameState ss = s.move(Move::move(c, cc));
        assert(ss.found[i] & c);
        ss.found[i] ^= c; // remove it from foundation
        ss.add_to_tab(j, c);
        ss.compute_moves_bound();
        recurse(ss, seen);
        if (moves_min > bound) return; } }
}

void recurse (GameState s) {
  /* This is a nice feature of C++; we can call recurse from main
     without requiring the creation of the hash table required at
     that point. */
  count = depth = 0;                 // HACK
  long estimated_capacity = 1UL<<26; // HACK
  unordered_map<int, int> seen(estimated_capacity);
  s.precompute();
  s.compute_moves_bound();
  recurse(s, seen); }
