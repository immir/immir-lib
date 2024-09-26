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

#define RUN_TIME 5
#define RUNUP_TIME 2.0
#define BATCH_SIZE 1024
#define WORK_SIZE 10

#define rtc() __builtin_ia32_rdtsc()

#define FIFO_ITEM_TYPE long
#include FIFO

long g_verbose = 0;

#define hash(a) ((a)*(a+1)*(a+2)*(a+3)*(a+4))

#include <time.h>
double wall () {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return t.tv_sec + t.tv_nsec/1e9; }

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

void reader (long_fifo_t fifo, long id, double stop, result_t *res) {
  double w0 = wall();
  int running = 0;
  long x = 0;
  stats_init(&res->s);
  while (1) {
    double w = wall();
    if (w > stop) break;
    if (!running && w - w0 > RUNUP_TIME) {
      stats_init(&res->s);
      running = 1; }
    for (int b = 0; b < BATCH_SIZE; b++) {
      // nanosleep?
      // usleep(rtc()&7);
      long a = 0, tm = rtc();
      int ret = long_fifo_pop(fifo, &a);
      tm = rtc() - tm;
      if (ret) {
        for (int i = 0; i < WORK_SIZE; i++)
          a = hash(a);
        x += a; }
      stats_update(&res->s, tm); } }
  res->x = x;
}

void writer (long_fifo_t fifo, long id, double stop, result_t *res) {
  double w0 = wall();
  int running = 0;
  stats_init(&res->s);
  while (1) {
    double w = wall();
    if (w > stop) break;
    if (!running && w - w0 > RUNUP_TIME) {
      stats_init(&res->s);
      running = 1; }
    for (int b = 0; b < BATCH_SIZE; b++) {
      // nanosleep?
      // usleep(rtc()&7);
      long a = 0;
      for (int i = 0; i < WORK_SIZE; i++)
        a = hash(a);
      long tm = rtc();
      int ret = long_fifo_append(fifo, a);
      (void) ret;
      tm = rtc() - tm;
      stats_update(&res->s, tm); } }
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

  printf("./b W=%ld R=%ld N=%ld M=%ld T=%ld\n", W,R,N,M,T);
  printf("implementation: %s (%s)\n", FIFO_METHOD, FIFO);

  double stop = wall() + RUN_TIME;

  // we'll use Open MP to simplify the creation of our threads
  omp_set_num_threads(W+R);
  result_t Wres[W], Rres[R];
  #pragma omp parallel
  {
    #pragma omp single
    {
      for (long i = 0; i < W; i++) {
        #pragma omp task
        writer(fifo, i, stop, &Wres[i]);
        if (g_verbose) printf("started writer %ld of %ld\n", i, W); }

      for (long i = 0; i < R; i++) {
        #pragma omp task
        reader(fifo, i, stop, &Rres[i]);
        if (g_verbose) printf("started reader %ld of %ld\n", i, W); }

      #pragma omp taskwait
    }
  }

  // accumulate results from all writing threads
  stats_t s_w;
  stats_init(&s_w);
  for (long i = 0; i < W; i++) {
    result_t r = Wres[i];
    stats_include(&s_w, r.s); }

  // accumulate results from all reading threads
  stats_t s_r;
  stats_init(&s_r);
  for (long i = 0; i < R; i++) {
    result_t r = Rres[i];
    stats_include(&s_r, r.s); }

  printf("stats for writers: "); stats_report(s_w);
  printf("stats for readers: "); stats_report(s_r);

  stats_t s = s_w;
  stats_include(&s, s_r);
  printf("overall stats: "); stats_report(s);

  long_fifo_destroy(fifo);
  printf("done\n");

  return 0;
}
