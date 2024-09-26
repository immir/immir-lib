#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

#ifndef FIFO_METHOD
#define FIFO_METHOD "mpmc single atomic spin-lock unpadded"
#endif

#ifndef FIFO_ITEM_TYPE
#error "FIFO_ITEM_TYPE required for fifo-mpmc.h"
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

#define FIFO_LOCK      FIFO_CONCAT1(FIFO_NAME, lock)
#define FIFO_UNLOCK    FIFO_CONCAT1(FIFO_NAME, unlock)

#ifndef FIFO_PAD
#define FIFO_PAD 0
#endif

typedef struct {
  atomic_long head;
#if FIFO_PAD
  long pad0[15];
#endif
  atomic_long tail;
#if FIFO_PAD
  long pad1[15];
#endif
  atomic_long lock;
  long limit;
  FIFO_ITEM_TYPE *items;
} *FIFO_TYPE;

void FIFO_LOCK(FIFO_TYPE fifo) {
  while (1) {
    long lock = 0;
    if (atomic_compare_exchange_weak_explicit(&fifo->lock, &lock, 1,
                                              memory_order_acquire,
                                              memory_order_relaxed)) break; } }

void FIFO_UNLOCK(FIFO_TYPE fifo) {
  atomic_store_explicit(&fifo->lock, 0, memory_order_release); }

long FIFO_CAPACITY(FIFO_TYPE fifo) {
  return fifo->limit - 1; /* limit is capacity + 1 */ }

long FIFO_SIZE(FIFO_TYPE fifo) {
  // TODO: the whole queue could change, even cycle around, in between
  // the loads here:
  long h = atomic_load_explicit(&fifo->head, memory_order_relaxed);
  long t = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
  long size = t - h;
  if (size < 0) size += fifo->limit;
  return size; }

static __attribute__((unused))
long FIFO_AVAIL(FIFO_TYPE fifo) {
  return FIFO_CAPACITY(fifo) - FIFO_SIZE(fifo); }

static __attribute__((unused))
int FIFO_APPEND(FIFO_TYPE fifo, FIFO_ITEM_TYPE x) {
  int ret = 0;
  FIFO_LOCK(fifo); // acquire
  do {
    long h = atomic_load_explicit(&fifo->head, memory_order_relaxed);
    long t = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
    long t1 = t == fifo->limit - 1 ? 0 : t+1;
    if (h == t1) break; // no space
    fifo->items[t] = x;
    atomic_store_explicit(&fifo->tail, t1, memory_order_relaxed);
    ret = 1;
  } while (0);
  FIFO_UNLOCK(fifo); // release
  return ret; }

static __attribute__((unused))
int FIFO_POP(FIFO_TYPE fifo, FIFO_ITEM_TYPE *x) {
  int ret = 0;
  FIFO_LOCK(fifo); // acquire
  do {
    long h = atomic_load_explicit(&fifo->head, memory_order_relaxed);
    long t = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
    if (h == t) break; // nothing to pop
    *x = fifo->items[h];
    long h1 = h == fifo->limit - 1 ? 0 : h+1;
    atomic_store_explicit(&fifo->head, h1, memory_order_relaxed);
    ret = 1;
  } while (0);
  FIFO_UNLOCK(fifo); // release
  return ret; }


static __attribute__((unused))
FIFO_TYPE FIFO_NEW(long capacity) {
  FIFO_TYPE fifo = malloc(sizeof *fifo);
  if (fifo == NULL) return NULL;
  fifo->limit = capacity + 1;
  fifo->items = malloc(fifo->limit * sizeof *fifo->items);
  if (!fifo->items) {
    free(fifo);
    return NULL; }
  atomic_store(&fifo->head, 0);
  atomic_store(&fifo->tail, 0);
  atomic_store(&fifo->lock, 0);
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

#undef FIFO_LOCK
#undef FIFO_UNLOCK

