#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include "crex.h"

// For brevity
typedef crex_allocator_t allocator_t;
typedef crex_context_t context_t;
typedef crex_match_t match_t;
typedef crex_regex_t regex_t;
typedef crex_status_t status_t;

// PCRE's allocators put the data parameter last rater than first
typedef struct {
  void *data;
  void *(*alloc)(size_t, void *);
  void (*free)(void *, void *);
} pcre_allocator_t;

typedef enum { BM_NONE, BM_TIME_MEMORY_CREX, BM_TIME_MEMORY_PCRE } benchmark_type_t;

typedef struct {
  benchmark_type_t benchmark_type;
  unsigned int compile_only : 1;

  void *(*create)(const void *);
  void (*destroy)(void *);

  void *(*compile_regex)(void *, const char *, size_t, size_t, const void *);
  void (*destroy_regex)(void *, void *);

  void (*run_test)(match_t *, void *, const void *, const char *, size_t);
} test_harness_t;

int run(int argc, char **argv, const test_harness_t *harness);

#endif
