#define main benchmark_main
#include "../a.c"
#undef main
#include <limits.h>

int main(int argc, char *argv[]) {
  assert(argc == 2);
  gf2_t g = {.m = 1, .n = 64, .b = 1};
  switch (atoi(argv[1])) {
    case 0: g.b = -1; break;
    case 1: g.b = 65; break;
    case 2: g.wds = 1; break;
    case 3: g.tablebits = -1; break;
    case 4: g.tablebits = sizeof(int)*CHAR_BIT - 1; break;
    case 5: g.kmax = -1; break;
    case 6: g.m = 0; break;
    case 7: g.n = 0; break;
    case 8: g.m = g.n = g.wds = INT_MAX; break;
    default: return 2;
  }
  gf2_init(&g);
  gf2_clear(&g);
  return 0;
}
