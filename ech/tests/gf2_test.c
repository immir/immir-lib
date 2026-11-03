#define main benchmark_main
#include "../a.c"
#undef main

static unsigned get_bit(const int wds, const uint64_t A[][wds],
                        const int r, const int c) {
  return (A[r][c / 64] >> (c % 64)) & 1;
}

static int reference_rank(const int m, const int n, const int wds,
                          const uint64_t A[m][wds]) {
  unsigned char B[m][n];
  for (int r = 0; r < m; r++)
    for (int c = 0; c < n; c++)
      B[r][c] = get_bit(wds, A, r, c);
  int rank = 0;
  for (int c = 0; c < n && rank < m; c++) {
    int pivot = rank;
    while (pivot < m && !B[pivot][c]) pivot++;
    if (pivot == m) continue;
    for (int j = 0; j < n; j++) {
      const unsigned char value = B[rank][j];
      B[rank][j] = B[pivot][j];
      B[pivot][j] = value;
    }
    for (int r = rank + 1; r < m; r++)
      if (B[r][c])
        for (int j = c; j < n; j++) B[r][j] ^= B[rank][j];
    rank++;
  }
  return rank;
}

static void check_matrix(gf2_t *g, const int expected_rank) {
  const int m = g->m, n = g->n, b = g->b, wds = g->wds;
  const uint64_t (*A)[wds] = g->matrix;
  uint64_t original[m][wds];
  for (int r = 0; r < m; r++)
    for (int w = 0; w < wds; w++) original[r][w] = A[r][w];
  const int rank = reference_rank(m, n, wds, original);
  assert(expected_rank < 0 || rank == expected_rank);
  const int augmented_rank = reference_rank(m, n + b, wds, original);
  const int count = gf2_kernel(g);
  assert(g->ech && g->rank == rank && g->corank == n - rank);
  assert(count == n - rank);
  const uint64_t (*K)[wds] = g->kernel;
  int free_column = 0;
  for (int i = 0; i < count; i++, free_column++) {
    while (free_column < n) {
      int r = 0;
      while (r < rank && g->pivots[r] != free_column) r++;
      if (r == rank) break;
      free_column++;
    }
    assert(free_column < n);
    for (int j = 0; j < count; j++)
      assert(get_bit(wds, K, j, free_column) == (unsigned)(i == j));
    for (int r = 0; r < m; r++) {
      unsigned parity = 0;
      for (int c = 0; c < n; c++)
        parity ^= get_bit(wds, original, r, c) & get_bit(wds, K, i, c);
      assert(parity == 0);
    }
  }
  uint64_t echelon[m][wds];
  for (int r = 0; r < m; r++)
    for (int w = 0; w < wds; w++) echelon[r][w] = A[r][w];
  const int solved = gf2_solution(g);
  for (int r = 0; r < m; r++)
    for (int w = 0; w < wds; w++) assert(A[r][w] == echelon[r][w]);
  assert(solved == (augmented_rank == rank ? b : -1));
  if (solved == b) {
    const uint64_t (*X)[wds] = g->solution;
    for (int i = 0; i < b; i++)
      for (int c = n; c < 64*wds; c++) assert(!get_bit(wds, X, i, c));
    for (int i = 0; i < b; i++)
      for (int r = 0; r < m; r++) {
        unsigned parity = 0;
        for (int c = 0; c < n; c++)
          parity ^= get_bit(wds, original, r, c) & get_bit(wds, X, i, c);
        assert(parity == get_bit(wds, original, r, n + i));
      }
  }
}

static void check_padding_and_rhs(void) {
  for (int n = 1; n <= 64; n += 63) {
    gf2_t g = {.m = n + 2, .n = n, .b = 1, .tablebits = 2};
    gf2_init(&g);
    const int wds = g.wds;
    uint64_t (*A)[wds] = g.matrix;
    for (int r = 0; r <= n; r++) A[r][r / 64] = UINT64_C(1) << (r % 64);
    assert(gf2_semi_ech(&g) == n);
    check_matrix(&g, n);
    gf2_clear(&g);
  }
}

static void check_randomization(void) {
  gf2_t g = {.m = 3, .n = 5, .b = 1};
  state[0] = 1; state[1] = 2;
  gf2_init(&g);
  check_matrix(&g, 0);
  gf2_randomize(&g);
  assert(!g.ech && g.rank == -1 && g.corank == -1);
  check_matrix(&g, -1);
  gf2_randomize(&g);
  assert(!g.ech && g.rank == -1 && g.corank == -1);
  assert(gf2_solution(&g) == g.b);
  assert(g.ech);
  check_matrix(&g, -1);
  gf2_clear(&g);
}

static void check_missing_pivots(void) {
  for (int S = 2; S <= 9; S++)
    for (int hole = 0; hole < S; hole++) {
      gf2_t g = {.m = 139, .n = 137, .b = 1, .tablebits = S};
      gf2_init(&g);
      const int wds = g.wds;
      uint64_t (*A)[wds] = g.matrix;
      int rank = 0;
      for (int c = 0; c < g.n; c++)
        if (c / S % 3 != 1 && c % S != hole) {
          A[c][c / 64] = UINT64_C(1) << (c % 64);
          if (c % 2) A[c][g.n / 64] |= UINT64_C(1) << (g.n % 64);
          rank++;
        }
      check_matrix(&g, rank);
      gf2_clear(&g);
    }
}

static void check_long_searches(void) {
  const int offsets[] = {0, 61, 63};
  const int gaps[] = {7, 8, 9, 15, 16, 17, 31, 32, 33, -1};
  for (int S = 6; S <= 8; S += 2)
    for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; i++)
      for (unsigned j = 0; j < sizeof gaps / sizeof *gaps; j++) {
        gf2_t g = {.m = 141, .n = 129, .b = 3, .tablebits = S};
        gf2_init(&g);
        const int wds = g.wds, target = offsets[i]+1;
        const int far = gaps[j] < 0 ? g.m-1 : target+gaps[j];
        uint64_t (*A)[wds] = g.matrix;
        for (int c = 0; c < g.n; c++) {
          const int row = c == target ? far : c == far ? target : c;
          A[row][c / 64] |= UINT64_C(1) << (c % 64);
          const int rhs = g.n + c % g.b;
          A[row][rhs / 64] |= UINT64_C(1) << (rhs % 64);
        }
        check_matrix(&g, g.n);
        gf2_clear(&g);
      }
}

static void check_rhs(void) {
  const int widths[] = {1, 7, 63, 64, 65, 129, 257};
  const int rhs[] = {1, 4, 8, 16, 32, 64};
  state[0] = 987; state[1] = 654;
  for (unsigned i = 0; i < sizeof widths / sizeof *widths; i++)
    for (unsigned j = 0; j < sizeof rhs / sizeof *rhs; j++)
      for (int shape = 0; shape < 3; shape++) {
        gf2_t g = {.m = shape ? widths[i]+3 : (widths[i]+1)/2,
                   .n = widths[i], .b = rhs[j]};
        gf2_init(&g);
        gf2_randomize(&g);
        const int wds = g.wds;
        uint64_t (*A)[wds] = g.matrix;
        for (int r = 0; r < g.m; r++) {
          uint64_t value = 0;
          for (int c = 0; c < g.n; c++) {
            if (shape == 2 || (shape == 1 && c % 8 == 0))
              A[r][c/64] &= ~(UINT64_C(1)<<(c%64));
            if (get_bit(wds, A, r, c))
              value ^= UINT64_C(0x9e3779b97f4a7c15)*(c+1);
          }
          for (int k = 0; k < g.b; k++) {
            const int c = g.n+k;
            A[r][c/64] &= ~(UINT64_C(1)<<(c%64));
            if ((value >> k) & 1) A[r][c/64] |= UINT64_C(1)<<(c%64);
          }
        }
        check_matrix(&g, shape == 2 ? 0 : -1);
        if (g.rank < g.m) {
          const int c = g.n+g.b-1;
          A[g.m-1][c/64] ^= UINT64_C(1)<<(c%64);
          assert(gf2_solution(&g) == -1);
        }
        gf2_clear(&g);
      }
}

int main(void) {
  check_padding_and_rhs();
  check_randomization();
  check_missing_pivots();
  check_long_searches();
  check_rhs();
  const int widths[] = {1, 2, 7, 63, 64, 65, 127, 128, 129};
  const int tablebits[] = {1, 2, 3, 8};
  state[0] = 123; state[1] = 456;
  int count = 0;
  for (unsigned i = 0; i < sizeof widths / sizeof *widths; i++)
    for (int shape = 0; shape < 3; shape++)
      for (unsigned t = 0; t < sizeof tablebits / sizeof *tablebits; t++)
        for (int pattern = 0; pattern < 4; pattern++) {
          const int n = widths[i];
          gf2_t g = {.m = shape == 0 ? (n + 1) / 2 : n + (shape == 2 ? 3 : 0),
                     .n = n, .b = pattern == 2 ? 64 : 3,
                     .tablebits = tablebits[t]};
          gf2_init(&g);
          gf2_randomize(&g);
          const int wds = g.wds;
          uint64_t (*A)[wds] = g.matrix;
          for (int r = 0; r < g.m; r++) {
            if (pattern == 1) A[r][0] &= ~UINT64_C(1);
            if (pattern == 2 && r > 0)
              for (int w = 0; w < wds; w++) A[r][w] = A[0][w];
            if (pattern == 3)
              for (int c = 1; c < n; c++)
                if (c % g.tablebits == g.tablebits - 1) {
                  const uint64_t mask = UINT64_C(1) << (c % 64);
                  A[r][c / 64] &= ~mask;
                  if (get_bit(wds, A, r, c - 1)) A[r][c / 64] |= mask;
                }
          }
          check_matrix(&g, -1);
          gf2_clear(&g);
          count++;
        }
  printf("gf2: padding/RHS, randomization, 44 missing-pivot cases, 60 long searches, 126 RHS cases, and %d rank/kernel/solution cases passed\n", count);
}
