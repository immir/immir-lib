#if 0
INC=${1:-fifo.h}
set -x
gcc -g -Wall -Werror -DFIFO="\"$INC\"" -O2 -fopenmp -o ${0%.*} $0 -lm && ./${0%.*}
exit
#endif

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <assert.h>

#include <omp.h>

#define rtc() __builtin_ia32_rdtsc()

#define FIFO_ITEM_TYPE long
#include FIFO

long g_verbose = 0;

typedef struct {
  double sum, sum2, min, max;
  long count;
} stats_t;

void stats_init(stats_t *s) {
  *s = (stats_t) {0};
  s->min = DBL_MAX;
}

void stats_update(stats_t *s, double x) {
  s->sum += x; s->sum2 += x*x;
  s->min = x < s->min ? x : s->min;
  s->max = x > s->max ? x : s->max;
  s->count++; }

void stats_include(stats_t *s, const stats_t t) {
  s->sum += t.sum;
  s->sum2 += t.sum2;
  s->min = s->min < t.min ? s->min : t.min;
  s->max = s->max > t.max ? s->max : t.max;
  s->count += t.count; }

void stats_report(const stats_t s) {
  double n = s.count;
  double avg = s.sum / n;
  double var = (s.sum2 - s.sum*s.sum/n) / (n-1);
  printf("avg %.2f, stddev %.2f, count %.0f [min %.2f, max %.2f]\n",
         avg, sqrt(var), (double)n, s.min, s.max); }

typedef struct {
  long x;          // "hash" of results
  stats_t s;
} result_t;

void reader (long_fifo_t fifo, long id, long count, result_t *res) {
  long x = 0;
  long nread = 0;
  stats_init(&res->s);
  while (nread < count) {
    // nanosleep?
    // usleep(rtc()&7);
    long a, tm = rtc();
    int ret = long_fifo_pop(fifo, &a);
    tm = rtc() - tm;
    stats_update(&res->s, tm);
    if (ret) nread++;
  }
  res->x = x;
}

void writer (long_fifo_t fifo, long id, long count, result_t *res) {
  long x = 0;
  stats_init(&res->s);
  while (count > 0) {
    // nanosleep?
    // usleep(rtc()&7);
    long tm = rtc();
    int ret = long_fifo_append(fifo, count);
    tm = rtc() - tm;
    stats_update(&res->s, tm);
    if (ret) count--;
  }
  res->x = x;
}

#define parse(x) strtol(x, NULL, 0)

int main (int argc, char *argv[]) {

  while (--argc && strchr(*++argv,'=')) putenv(*argv);

  long W = parse(getenv("W") ?: "1");    // number of writing threads
  long R = parse(getenv("R") ?: "1");    // number of reading threads
  long N = parse(getenv("N") ?: "1000"); // size of fifo
  long M = parse(getenv("M") ?: "1000"); // number of items to push/pop per thread
  long T = parse(getenv("T") ?: "1");    // number of trials to average

  g_verbose = parse(getenv("v") ?: "0"); // HACK: global

  long_fifo_t fifo = long_fifo_new(N);
  assert(fifo);

  printf("./a W=%ld R=%ld N=%ld M=%ld T=%ld\n", W,R,N,M,T);
  printf("implementation: %s (%s)\n", FIFO_METHOD, FIFO);

  // we'll use Open MP to simplify the creation of our threads
  omp_set_num_threads(W+R);
  result_t Wres[W], Rres[R];
  #pragma omp parallel
  {
    #pragma omp single
    {
      for (long i = 0; i < W; i++) {
        #pragma omp task
        writer(fifo, i, M, &Wres[i]);
        if (g_verbose) printf("started writer %ld of %ld\n", i, W); }

      for (long i = 0; i < R; i++) {
        #pragma omp task
        reader(fifo, i, M, &Rres[i]);
        if (g_verbose) printf("started reader %ld of %ld\n", i, W); }

      #pragma omp taskwait
    }
  }

  long xw = 0, xr = 0;

  // accumulate results from all writing threads
  stats_t s_w;
  stats_init(&s_w);
  for (long i = 0; i < W; i++) {
    result_t r = Wres[i];
    stats_include(&s_w, r.s);
    xw += r.x; }

  // accumulate results from all reading threads
  stats_t s_r;
  stats_init(&s_r);
  for (long i = 0; i < R; i++) {
    result_t r = Rres[i];
    stats_include(&s_r, r.s);
    xr += r.x; }

  printf("stats for writers: "); stats_report(s_w);
  printf("stats for readers: "); stats_report(s_r);

  stats_t s = s_w;
  stats_include(&s, s_r);
  printf("overall stats: "); stats_report(s);

  if (xw == xr)
    printf("check agrees between writers and readers\n");
  else {
    printf("WARNING: disagreement between writers and readers!\n");
    exit(1); }

  long_fifo_destroy(fifo);
  printf("done\n");


  {
    stats_t s;
    stats_init(&s);
    stats_update(&s, 10);
    stats_update(&s, 11);
    stats_update(&s, 12);
    stats_update(&s, 13);
    stats_update(&s, 14);
    stats_update(&s, 15);
    stats_update(&s, 16);
    stats_update(&s, 17);
    stats_update(&s, 18);
    stats_update(&s, 19);
    stats_update(&s, 20);
    printf("stats 10..20: "); stats_report(s);
  }

  return 0;
}
