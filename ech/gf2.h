#ifndef GF2_H
#define GF2_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>

// prototypes of the external functions that actually do the heavy lifting...

static int semi_ech(const int m, const int n, const int wds, uint64_t (*A)[wds],
                    int S, int *piv);

static int kernel(const int m,    // height (rows)
                  const int n,    // width in bits
                  const int wds,  // width in words
                  uint64_t (*A)[wds],
                  const int *piv, // pivots computed during semi-ech
                  const int k,    // number of kernel rows requested
                  uint64_t (*K)[wds]);

int solution(const int m,    // height (rows)
             const int n,    // width in bits
             const int wds,  // width in words
             uint64_t (*A)[wds],
             const int *piv, // pivots computed during semi-ech
             const int b,    // number of rhs columns
             uint64_t (*X)[wds]);


// ----------------------------------------------------------------------
// data structure for managing information related to solving
// linear systems over GF(2)...

typedef struct {
  // --- required user input parameters:
  int m;              // number of rows
  int n;              // number of (bit) columns
  int b;              // number of rhs columns
  // --- optional paramters (defaults will be computed)
  int wds;            // width of matrix in words
  int kmax;           // max number of kernel vectors (user can set to limit them)
  int tablebits;      // number of bits for 4 Russians tables
  // --- flags
  int inited;         // inited?
  int ech;            // flag to indicate semi-ech form
  // flags to indicate the corresponding arrays need to be free'd
  // (the user may have provided their own arrays, for example)
  int free_matrix, free_kernel, free_solution, free_pivots;
  // --- storage
  void *matrix;       // flattened array of m * wds words
  void *kernel;       // flattened array of k * wds words
  void *solution;     // if non-null, a particular solution  (b * wds words)
  int  *pivots;       // pivots
  // --- computed data
  int rank;           // rank of system
  int corank;         // rank of kernel
} gf2_t;


void gf2_init(gf2_t *data) {
  assert(data->n > 0);
  assert(data->m > 0);
  // user may set data->wds to save space for a RHS or book-keeping columns
  if (data->wds == 0) 
    data->wds = (data->n + data->b + 64-1)/64;
  if (data->tablebits == 0)
    data->tablebits = log(data->m);
  // user may allocate their own array space
  if (data->matrix == NULL) {
    data->matrix = calloc(data->m * data->wds, sizeof(uint64_t));
    assert(data->matrix);
    data->free_matrix = 1;
  }
  // user may allocate their own pivot data
  if (data->pivots == NULL) {
    data->pivots = malloc(data->m * sizeof *data->pivots);
    assert(data->pivots);
    data->free_pivots = 1;
  }
  data->rank = -1;
  data->ech = 0;
  data->inited = 1;
}


void gf2_clear(gf2_t *data) {
  if (data->pivots && data->free_pivots)
    { free(data->pivots); data->pivots = NULL; }
  if (data->matrix && data->free_matrix)
    { free(data->matrix); data->matrix = NULL; }
  if (data->kernel && data->free_kernel)
    { free(data->kernel); data->kernel = NULL; }
  if (data->solution && data->free_solution)
    { free(data->solution); data->solution = NULL; }
  data->inited = 0;
}


int gf2_semi_ech(gf2_t *data) {
  // while (void *) might be evil, the user is responsible for ensuring A points
  // to an array of the appropriate form
  data->rank   = semi_ech(data->m, data->n, data->wds, data->matrix, 
                          data->tablebits, data->pivots);
  data->corank = data->n - data->rank;
  data->ech = 1;
  return data->rank;
}


int gf2_kernel(gf2_t *data) {
  if (!data->ech)
    gf2_semi_ech(data);
  if (data->corank == 0) return 0;
  if (data->kmax == 0)
    data->kmax = data->corank;
  if (data->kernel == NULL) {
    data->kernel = calloc(data->kmax * data->wds, sizeof(uint64_t));
    assert(data->kernel);
    data->free_kernel = 1;
  }
  return kernel(data->m, data->n, data->wds, data->matrix, data->pivots,
                data->kmax, data->kernel);
}

int gf2_solution(gf2_t *data) {
  if (data->b == 0)
    return -1;
  if (!data->ech)
    gf2_semi_ech(data);
  if (data->solution == NULL) {
    data->solution = calloc(data->b * data->wds, sizeof(uint64_t));
    assert(data->solution);
    data->free_solution = 1;
  }
  return solution(data->m, data->n, data->wds, data->matrix, data->pivots,
                  data->b, data->solution);
}


void gf2_randomize(gf2_t *data) {
  // NB: we don't care about zeroing out unused bits
  uint64_t rand64();
  assert(data->inited);
  uint64_t *arr = data->matrix;
  for (int i = 0; i < data->m; i++)
    for (int w = 0; w < data->wds; w++)
      *arr++ = rand64();
}

void gf2_info(gf2_t *data) {
  printf("gf2{ m=%d, n=%d, wds=%d, rank=%d, corank=%d }\n",
         data->m, data->n, data->wds, data->rank, data->corank);
}

#endif
