#ifndef HARNESS_H
#define HARNESS_H

#include <stddef.h>
#include <time.h>

#include "execution-engine.h"
#include "suite.h"

suite_t *load_suites(char **paths, size_t n_paths);
void destroy_suites(suite_t *suites, size_t n_suites);

const execution_engine_t **load_engines(size_t *n_engines,
                                        const execution_engine_t *all_engines[],
                                        char **names,
                                        size_t n_names,
                                        const execution_engine_t *default_engine);

void destroy_engines(const execution_engine_t **engines);

typedef struct timespec bench_timer_t;

void start_timer(bench_timer_t *timer);
double stop_timer(bench_timer_t *timer);

typedef enum { AT_NONE, AT_CREX, AT_PCRE } allocator_type_t;

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

void reset_allocator(allocator_t *allocator, ex_convention_t convention, int measure);

size_t parse_size(char *str);

#endif
