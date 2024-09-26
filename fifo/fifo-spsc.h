#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

#ifndef FIFO_METHOD
#define FIFO_METHOD "spsc atomic head/tail unpadded"
#endif

#ifndef FIFO_ITEM_TYPE
#error "FIFO_ITEM_TYPE required by fifo.h"
#endif

#ifndef FIFO_NAME
#define FIFO_NAME FIFO_CONCAT1(FIFO_ITEM_TYPE, fifo)
#endif

#define FIFO_CONCAT1(X,Y)  FIFO_CONCAT2(X,Y)
#define FIFO_CONCAT2(X,Y)  X ## _ ## Y

#define FIFO_TYPE      FIFO_CONCAT1(FIFO_NAME, t)
#define FIFO_INIT      FIFO_CONCAT1(FIFO_NAME, init)
#define FIFO_NEW       FIFO_CONCAT1(FIFO_NAME, new)
#define FIFO_DESTROY   FIFO_CONCAT1(FIFO_NAME, destroy)
#define FIFO_CAPACITY  FIFO_CONCAT1(FIFO_NAME, capacity)
#define FIFO_SIZE      FIFO_CONCAT1(FIFO_NAME, size)
#define FIFO_AVAIL     FIFO_CONCAT1(FIFO_NAME, avail)
#define FIFO_APPEND    FIFO_CONCAT1(FIFO_NAME, append)
#define FIFO_POP       FIFO_CONCAT1(FIFO_NAME, pop)

#define FIFO_MIN(a,b)  ((a)<(b)?(a):(b))

#ifndef FIFO_PAD
#define FIFO_PAD 0
#endif

typedef struct {
  atomic_long head;
#if FIFO_PAD
  long pad0[15];
#endif
  atomic_long tail;
  long limit;
  FIFO_ITEM_TYPE *items;
} *FIFO_TYPE;

static __attribute__((unused))
long FIFO_CAPACITY(FIFO_TYPE fifo) {
  return fifo->limit - 1; } // limit is capacity + 1

static __attribute__((unused))
long FIFO_SIZE(FIFO_TYPE fifo) {
  long h = atomic_load_explicit(&fifo->head, memory_order_relaxed);
  long t = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
  long size = t - h;
  if (size < 0) size += fifo->limit;
  return size; }

static __attribute__((unused))
long FIFO_AVAIL(FIFO_TYPE fifo) {
  long h = atomic_load_explicit(&fifo->head, memory_order_relaxed);
  long t = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
  long avail = h - t - 1;
  if (avail < 0) avail += fifo->limit;
  return avail; }

static __attribute__((unused))
long FIFO_APPEND(FIFO_TYPE fifo, FIFO_ITEM_TYPE x) {
  long h = atomic_load_explicit(&fifo->head, memory_order_relaxed);
  long t = atomic_load_explicit(&fifo->tail, memory_order_acquire);
  long t1 = t+1 == fifo->limit ? 0 : t+1;
  if (h == t1) return 0;
  fifo->items[t] = x;
  atomic_store_explicit(&fifo->tail, t1, memory_order_release);
  return 1; }

static __attribute__((unused))
long FIFO_POP(FIFO_TYPE fifo, FIFO_ITEM_TYPE *x) {
  long h = atomic_load_explicit(&fifo->head, memory_order_acquire);
  long t = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
  if (h == t) return 0;
  *x = fifo->items[h];
  long h1 = (h+1) == fifo->limit ? 0 : h+1;
  atomic_store_explicit(&fifo->head, h1, memory_order_release);
  return 1; }

static __attribute__((unused))
int FIFO_INIT(FIFO_TYPE fifo, long capacity) {
  fifo->limit = capacity + 1;
  fifo->items = malloc(fifo->limit * sizeof *fifo->items);
  if (!fifo->items) return 1; /* failure */
  atomic_store_explicit(&fifo->head, 0, memory_order_relaxed);
  atomic_store_explicit(&fifo->tail, 0, memory_order_relaxed);
  return 0; /* success */ }

static __attribute__((unused))
FIFO_TYPE FIFO_NEW(long capacity) {
  FIFO_TYPE fifo = malloc(sizeof *fifo);
  if (fifo == NULL) return NULL;
  int init_fail = FIFO_INIT(fifo, capacity);
  if (init_fail) {
    free(fifo);
    return NULL; }
  return fifo; }

static __attribute__((unused))
void FIFO_DESTROY(FIFO_TYPE fifo) {
  free(fifo->items);
  free(fifo); }


#undef FIFO_ITEM_TYPE
#undef FIFO_NAME

#undef FIFO_TYPE
#undef FIFO_INIT
#undef FIFO_NEW
#undef FIFO_DESTROY
#undef FIFO_CAPACITY
#undef FIFO_SIZE
#undef FIFO_AVAIL
#undef FIFO_APPEND
#undef FIFO_POP
#undef FIFO_PAD
