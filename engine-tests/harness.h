#ifndef HARNESS_H
#define HARNESS_H

#include <stddef.h>
#include <time.h>

#include "suite.h"

suite_t *load_suites(char **paths, size_t n_paths);
void destroy_suites(suite_t *suites, size_t n_suites);

typedef struct timespec bench_timer_t;

void start_timer(bench_timer_t *timer);
double stop_timer(bench_timer_t *timer);

typedef enum { AT_NONE, AT_CREX, AT_PCRE } allocator_type_t;

typedef struct {
  void *data;
  void *(*alloc)(size_t, void *);
  void (*free)(void *, void *);
} pcre_allocator_t;

typedef struct {
  size_t n_allocations;
  size_t total_size;
} bm_alloc_stats_t;

typedef struct {
  union {
    crex_allocator_t crex;
    pcre_allocator_t pcre;
  } u;

  bm_alloc_stats_t stats;
} allocator_t;

void reset_allocator(allocator_t *allocator, allocator_type_t type, int measure);

size_t parse_size(char *str);

#endif
