#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

/* Note: this version doesn't have O(1) size or avail functions, so we just
   publish "is empty" and "is full" functions instead. */

#ifndef FIFO_METHOD
#define FIFO_METHOD "1024core mpmc"
#endif

#ifndef FIFO_ITEM_TYPE
#error "FIFO_ITEM_TYPE required"
#endif

#ifndef FIFO_NAME
#define FIFO_NAME FIFO_CONCAT1(FIFO_ITEM_TYPE, fifo)
#endif

#define FIFO_CONCAT1(X,Y)  FIFO_CONCAT2(X,Y)
#define FIFO_CONCAT2(X,Y)  X ## _ ## Y

#define FIFO_TYPE      FIFO_CONCAT1(FIFO_NAME, t)
#define FIFO_SEQ_DATA  FIFO_CONCAT1(FIFO_NAME, seq_data)
#define FIFO_INIT      FIFO_CONCAT1(FIFO_NAME, init)
#define FIFO_NEW       FIFO_CONCAT1(FIFO_NAME, new)
#define FIFO_DESTROY   FIFO_CONCAT1(FIFO_NAME, destroy)
#define FIFO_CAPACITY  FIFO_CONCAT1(FIFO_NAME, capacity)
#define FIFO_EMPTY     FIFO_CONCAT1(FIFO_NAME, empty)
#define FIFO_FULL      FIFO_CONCAT1(FIFO_NAME, full)
#define FIFO_APPEND    FIFO_CONCAT1(FIFO_NAME, append)
#define FIFO_POP       FIFO_CONCAT1(FIFO_NAME, pop)

#ifndef FIFO_PAD
#define FIFO_PAD 0
#endif

typedef struct {
  atomic_long seq;
  FIFO_ITEM_TYPE data;
} FIFO_SEQ_DATA;

typedef struct {
  long limit;
  atomic_long head;
#if FIFO_PAD
  long pad0[15];
#endif
  atomic_long tail;
  FIFO_SEQ_DATA *items;
} *FIFO_TYPE;

long FIFO_CAPACITY(FIFO_TYPE fifo) {
  return fifo->limit; }

static __attribute__((unused))
long FIFO_EMPTY(FIFO_TYPE fifo) {
  long pos = atomic_load_explicit(&fifo->head, memory_order_relaxed);
  FIFO_SEQ_DATA *item = &fifo->items[pos % fifo->limit];
  long seq = atomic_load_explicit(&item->seq, memory_order_relaxed);
  long del = seq - (pos + 1);
  return del < 0; }

static __attribute__((unused))
long FIFO_FULL(FIFO_TYPE fifo) {
  long pos = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
  FIFO_SEQ_DATA *item = &fifo->items[pos % fifo->limit];
  long seq = atomic_load_explicit(&item->seq, memory_order_relaxed);
  long del = seq - pos;
  return del < 0; }

static __attribute__((unused))
int FIFO_APPEND(FIFO_TYPE fifo, FIFO_ITEM_TYPE x) {
  FIFO_SEQ_DATA *item;
  long pos = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
  for (;;) {
    item = &fifo->items[pos % fifo->limit];
    long seq = atomic_load_explicit(&item->seq, memory_order_acquire);
    long del = seq - pos;
    if (del < 0)
      return 0;
    if (del == 0)
      if (atomic_compare_exchange_weak_explicit(&fifo->tail, &pos, pos+1,
                                                memory_order_relaxed,
                                                memory_order_relaxed))
        break;
    pos = atomic_load_explicit(&fifo->tail, memory_order_relaxed);
  }
  item->data = x;
  atomic_store_explicit(&item->seq, pos + 1, memory_order_release);
  return 1; }

static __attribute__((unused))
int FIFO_POP(FIFO_TYPE fifo, FIFO_ITEM_TYPE *x) {
  FIFO_SEQ_DATA *item;
  long pos = atomic_load_explicit(&fifo->head, memory_order_relaxed);
  for (;;) {
    item = &fifo->items[pos % fifo->limit];
    long seq = atomic_load_explicit(&item->seq, memory_order_acquire);
    long del = seq - (pos + 1);
    if (del < 0)
      return 0;
    if (del == 0)
      if (atomic_compare_exchange_weak_explicit(&fifo->head, &pos, pos + 1,
                                                memory_order_relaxed,
                                                memory_order_relaxed))
        break;
    pos = atomic_load_explicit(&fifo->head, memory_order_relaxed);
  }
  *x = item->data;
  atomic_store_explicit(&item->seq, pos + fifo->limit, memory_order_release);
  return 1; }

static __attribute__((unused))
int FIFO_INIT(FIFO_TYPE fifo, long capacity) {
  fifo->limit = capacity;
  fifo->items = malloc(fifo->limit * sizeof *fifo->items);
  if (!fifo->items)
    return 1 /* failure */;
  atomic_store_explicit(&fifo->head, 0, memory_order_relaxed);
  atomic_store_explicit(&fifo->tail, 0, memory_order_relaxed);
  for (long i = 0; i < fifo->limit; i++)
    atomic_store_explicit(&fifo->items[i].seq, i, memory_order_relaxed);
  return 0 /* success */; }

static __attribute__((unused))
FIFO_TYPE FIFO_NEW(long capacity) {
  FIFO_TYPE fifo = malloc(sizeof *fifo);
  if (fifo == NULL) return NULL;
  if (FIFO_INIT(fifo, capacity)) {
    free(fifo);
    return NULL; }
  return fifo; }

static __attribute__((unused))
void FIFO_DESTROY(FIFO_TYPE fifo) {
  free(fifo->items);
  free(fifo); }

#undef FIFO_ITEM_TYPE
#undef FIFO_SEQ_DATA
#undef FIFO_NAME

#undef FIFO_TYPE
#undef FIFO_INIT
#undef FIFO_NEW
#undef FIFO_DESTROY
#undef FIFO_CAPACITY
#undef FIFO_FULL
#undef FIFO_EMPTY
#undef FIFO_APPEND
#undef FIFO_POP
