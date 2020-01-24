#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crex.h"

#if defined(__x86_64__) && !defined(NO_NATIVE_COMPILER)
#define NATIVE_COMPILER
#endif

#if (defined(__GNUC__) || defined(__clang__))

// Maybe Unused
#define MU __attribute__((unused))

#define UNREACHABLE()                                                                              \
  do {                                                                                             \
    assert(0);                                                                                     \
    __builtin_unreachable();                                                                       \
  } while (0)

#else

#define MU
#define UNREACHABLE() assert(0)

#endif

#define WUR CREX_WARN_UNUSED_RESULT

typedef crex_allocator_t allocator_t;
typedef crex_context_t context_t;
typedef crex_match_t match_t;
typedef crex_status_t status_t;
typedef crex_regex_t regex_t;

#define ALLOC(allocator, size) ((allocator)->alloc)((allocator)->context, size)
#define FREE(allocator, pointer) ((allocator)->free)((allocator)->context, pointer)

#endif
