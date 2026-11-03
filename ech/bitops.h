#ifndef LINCOMP_BITOPS_H
#define LINCOMP_BITOPS_H

#if __has_include(<stdbit.h>)
#include <stdbit.h>
#else
#include <limits.h>

// Older HPC libc installations lack stdbit.h even with a C23 compiler.
static inline unsigned int stdc_count_ones_ull(unsigned long long value) {
  return (unsigned int)__builtin_popcountll(value);
}

static inline unsigned int stdc_trailing_zeros_ull(unsigned long long value) {
  return value ? (unsigned int)__builtin_ctzll(value)
               : (unsigned int)(sizeof value * CHAR_BIT);
}
#endif

#endif
